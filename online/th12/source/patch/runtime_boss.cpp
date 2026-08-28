#include "runtime_boss.h"

#include "boss_health.h"
#include "log.h"
#include "memory_patch.h"
#include "version_map.h"

#include <windows.h>

#include <array>
#include <sstream>

extern "C" {

int32_t __stdcall Phase12RegisterBossLife(int32_t baseLife, void* enemy);
int32_t __stdcall Phase12ScaleBossDamage(int32_t damage, void* enemy);
void __stdcall Phase12ObserveSpellStart(void* enemy);
uintptr_t Phase12EnemyReadIntParameter =
    coop::th12::GameAddresses::kEnemyReadIntParameter;
uintptr_t Phase12OriginalBossDamageApply =
    coop::th12::GameAddresses::kEnemyDamageApply;
uintptr_t Phase12OriginalSpellStart =
    coop::th12::GameAddresses::kEnemySpellStart;

__declspec(naked) void Phase12BossLifeReadHook() {
    __asm {
        // The original ECL integer-parameter reader receives the enemy in EAX
        // and the parameter index in ECX. Retain the enemy across that call.
        push eax
        call dword ptr [Phase12EnemyReadIntParameter]
        pop ecx

        // 0x00400000 is set by the native Boss-registration opcode. The
        // following native set-life code uses the same bit for Boss setup.
        test dword ptr [ecx + 16B8h], 00400000h
        jz unchanged
        push ecx
        push eax
        call Phase12RegisterBossLife
    unchanged:
        ret
    }
}

__declspec(naked) void Phase12BossDamageApplyHook() {
    __asm {
        // The native call receives &enemy->currentLife in ECX and the
        // accumulated damage in EAX. Keep ECX intact for the original call.
        push ecx
        mov edx, ecx
        sub edx, 1608h
        test dword ptr [edx + 16B8h], 00400000h
        jz apply
        push edx
        push eax
        call Phase12ScaleBossDamage
    apply:
        pop ecx
        call dword ptr [Phase12OriginalBossDamageApply]
        ret
    }
}

__declspec(naked) void Phase12SpellStartHook() {
    __asm {
        push dword ptr [esp + 4]
        call dword ptr [Phase12OriginalSpellStart]
        push ebx
        call Phase12ObserveSpellStart
        ret 4
    }
}

}  // extern "C"

namespace coop::th12 {
namespace {

RelativeCallPatch g_bossLifeReadPatch(
    GameAddresses::kEnemySetLifeReadCall,
    GameAddresses::kEnemyReadIntParameter);
RelativeCallPatch g_bossDamageApplyPatch(
    GameAddresses::kEnemyDamageApplyCall,
    GameAddresses::kEnemyDamageApply);
RelativeCallPatch g_spellStartPatch(
    GameAddresses::kEnemySpellStartCall,
    GameAddresses::kEnemySpellStart);
std::wstring g_root;
uint32_t g_scaleCount = 0;
void* g_lastBoss = nullptr;
int32_t g_lastBaseLife = 0;

struct BossScaleRecord {
    void* enemy{};
    uint32_t generation{};
    uint32_t damageRemainder{};
    int32_t baseLife{};
};

std::array<BossScaleRecord, 16> g_bossScaleRecords{};
size_t g_nextBossScaleRecord = 0;

BossScaleRecord* FindBossScaleRecord(void* enemy) noexcept {
    for (auto& record : g_bossScaleRecords) {
        if (record.enemy == enemy) {
            return &record;
        }
    }
    return nullptr;
}

BossScaleRecord& TrackBossScale(void* enemy) noexcept {
    if (BossScaleRecord* record = FindBossScaleRecord(enemy)) {
        return *record;
    }
    BossScaleRecord& record = g_bossScaleRecords[g_nextBossScaleRecord];
    g_nextBossScaleRecord =
        (g_nextBossScaleRecord + 1) % g_bossScaleRecords.size();
    record = {};
    record.enemy = enemy;
    return record;
}

}  // namespace

bool InitializeRuntimeBosses(const std::wstring& root,
                             std::wstring& error) noexcept {
    const std::wstring configPath = root + L"\\coop\\config.ini";
    if (GetPrivateProfileIntW(L"phase12", L"enabled", 1,
                              configPath.c_str()) == 0) {
        coop::WriteLog(root, L"patch.log",
                       L"phase12 Boss HP scaling disabled by config");
        return true;
    }

    g_root = root;
    g_scaleCount = 0;
    g_lastBoss = nullptr;
    g_lastBaseLife = 0;
    g_bossScaleRecords = {};
    g_nextBossScaleRecord = 0;
    if (!g_bossLifeReadPatch.Install(
            reinterpret_cast<void*>(&Phase12BossLifeReadHook))) {
        error = L"phase 12 Boss set-life call-site signature mismatch";
        return false;
    }
    if (!g_bossDamageApplyPatch.Install(
            reinterpret_cast<void*>(&Phase12BossDamageApplyHook))) {
        g_bossLifeReadPatch.Restore();
        error = L"phase 12 Boss damage call-site signature mismatch";
        return false;
    }
    if (!g_spellStartPatch.Install(
            reinterpret_cast<void*>(&Phase12SpellStartHook))) {
        g_bossDamageApplyPatch.Restore();
        g_bossLifeReadPatch.Restore();
        error = L"phase 12 spell-start call-site signature mismatch";
        return false;
    }
    return true;
}

uint32_t RuntimeBossLifeScaleCount() noexcept {
    return g_scaleCount;
}

uint64_t RuntimeBossDamageScaleHash() noexcept {
    // This state is not part of the game object, so include it in the
    // end-of-frame hash to catch an asymmetric fractional-damage carry.
    uint64_t hash = 14695981039346656037ULL;
    auto mix = [&hash](uint32_t value) {
        for (uint32_t shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<uint8_t>(value >> shift);
            hash *= 1099511628211ULL;
        }
    };
    mix(g_scaleCount);
    for (const auto& record : g_bossScaleRecords) {
        mix(record.generation);
        mix(record.damageRemainder);
        mix(static_cast<uint32_t>(record.baseLife));
    }
    return hash;
}

}  // namespace coop::th12

extern "C" int32_t __stdcall Phase12RegisterBossLife(int32_t baseLife,
                                                       void* enemy) {
    ++coop::th12::g_scaleCount;
    coop::th12::g_lastBoss = enemy;
    coop::th12::g_lastBaseLife = baseLife;
    auto& record = coop::th12::TrackBossScale(enemy);
    ++record.generation;
    if (record.generation == 0) {
        record.generation = 1;
    }
    record.damageRemainder = 0;
    record.baseLife = baseLife;
    std::wstringstream message;
    message << L"phase12 Boss phase effective HP armed; sequence="
            << coop::th12::g_scaleCount << L"; base=" << baseLife
            << L"; native_current_max_retained=1; damage_ratio=2/3";
    coop::WriteLog(coop::th12::g_root, L"patch.log", message.str());
    return baseLife;
}

extern "C" int32_t __stdcall Phase12ScaleBossDamage(int32_t damage,
                                                      void* enemy) {
    auto& record = coop::th12::TrackBossScale(enemy);
    return coop::ScaleBossDamageFor150PercentLife(damage,
                                                   record.damageRemainder);
}

extern "C" void __stdcall Phase12ObserveSpellStart(void* enemy) {
    if (enemy == nullptr) {
        return;
    }
    auto* bytes = static_cast<uint8_t*>(enemy);
    const int32_t current =
        *reinterpret_cast<const int32_t*>(bytes + 0x1608);
    const int32_t maximum =
        *reinterpret_cast<const int32_t*>(bytes + 0x160C);
    auto* record = coop::th12::FindBossScaleRecord(enemy);
    const bool tracked = record != nullptr && record->generation != 0;
    std::wstringstream message;
    message << L"phase12 spell start observed; sequence="
            << coop::th12::g_scaleCount << L"; tracked="
            << (tracked ? 1 : 0)
            << L"; base=" << coop::th12::g_lastBaseLife
            << L"; current=" << current << L"; maximum=" << maximum
            << L"; native_hp_coordinates=1; damage_remainder="
            << (record != nullptr ? record->damageRemainder : 0);
    coop::WriteLog(coop::th12::g_root, L"patch.log", message.str());
}
