#include "runtime_collision.h"

#include "collision_ownership.h"
#include "log.h"
#include "memory_patch.h"
#include "runtime_bomb.h"
#include "runtime_determinism.h"
#include "runtime_player2.h"
#include "runtime_player_context.h"
#include "runtime_resource_tx.h"
#include "runtime_resources.h"
#include "version_map.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

extern "C" {

int __stdcall Phase9DispatchRectCollision(void* first, void* second);
int __stdcall Phase9DispatchCircleCollision(void* first, void* second);
int __stdcall Phase9DispatchRotatedCollision(void* first, void* second,
                                             void* third, void* fourth);
void* __stdcall Phase9SelectItemOwner(void* item);
void __stdcall Phase9FinishItemSlot();
void* __stdcall Phase9SelectAimTarget(void* sourcePosition);

__declspec(naked) void Phase9RectCollisionHook() {
    __asm {
        push ecx
        push eax
        call Phase9DispatchRectCollision
        ret
    }
}

__declspec(naked) void Phase9CircleCollisionHook() {
    __asm {
        push dword ptr [esp + 4]
        push eax
        call Phase9DispatchCircleCollision
        ret 4
    }
}

__declspec(naked) void Phase9RotatedCollisionHook() {
    __asm {
        push dword ptr [esp + 0Ch]
        push dword ptr [esp + 0Ch]
        push dword ptr [esp + 0Ch]
        push eax
        call Phase9DispatchRotatedCollision
        ret 0Ch
    }
}

__declspec(naked) void Phase9ItemBeginHook() {
    __asm {
        mov eax, dword ptr [edi + 9B0h]
        test eax, eax
        jz inactive
        push ecx
        push edx
        push edi
        call Phase9SelectItemOwner
        pop edx
        pop ecx
        mov eax, dword ptr [edi + 9B0h]
    inactive:
        ret
    }
}

__declspec(naked) void Phase9AimTargetHook() {
    __asm {
        push ecx
        push edx
        push ecx
        call Phase9SelectAimTarget
        pop edx
        pop ecx
        ret
    }
}

__declspec(naked) void Phase9ItemAdvanceHook() {
    __asm {
        push ecx
        push edx
        call Phase9FinishItemSlot
        pop edx
        pop ecx
        mov eax, dword ptr [esp + 14h]
        inc eax
        ret
    }
}

int __stdcall Phase9PlayerDamageHook(void* position, void* hitbox);
void __stdcall Phase9GrazeSettlementHook(void* position);

}  // extern "C"

namespace coop::th12 {
namespace {

constexpr uint32_t kPositionXOffset = 0x97C;
constexpr uint32_t kPositionYOffset = 0x980;
constexpr uint32_t kPlayerStateOffset = 0xA28;
constexpr uint32_t kPlayerStateTimerPreviousOffset = 0xA30;
constexpr uint32_t kPlayerStateTimerCurrentOffset = 0xA34;
constexpr uint32_t kPlayerBulletPoolOffset = 0xA58;
constexpr uint32_t kPlayerBulletStride = 0x78;
constexpr uint32_t kPlayerBulletCapacity = 0x100;
constexpr uint32_t kPlayerBulletStateOffset = 0x48;
constexpr uint32_t kPlayerBulletDamageOffset = 0x60;
constexpr uint32_t kPlayerBulletShotRecordOffset = 0x74;
constexpr uint32_t kShotRecordOptionIndexOffset = 0x1C;
constexpr uint32_t kItemPositionXOffset = 0x96C;
constexpr uint32_t kItemPositionYOffset = 0x970;
constexpr uint32_t kItemStateOffset = 0x9B0;
constexpr uint32_t kItemTypeOffset = 0x9B4;
constexpr float kUfoTokenControlDistanceSquared = 3600.0f;
constexpr uint32_t kItemBoundsLeftOffset = 0xC444;
constexpr uint32_t kItemBoundsBottomOffset = 0xC448;
constexpr uint32_t kItemBoundsRightOffset = 0xC450;
constexpr uint32_t kItemBoundsTopOffset = 0xC454;
constexpr uint32_t kItemFocusBoundsLeftOffset = 0xC45C;
constexpr uint32_t kItemFocusBoundsBottomOffset = 0xC460;
constexpr uint32_t kItemFocusBoundsRightOffset = 0xC468;
constexpr uint32_t kItemFocusBoundsTopOffset = 0xC46C;
constexpr uint32_t kItemNormalBoundsLeftOffset = 0xC474;
constexpr uint32_t kItemNormalBoundsBottomOffset = 0xC478;
constexpr uint32_t kItemNormalBoundsRightOffset = 0xC480;
constexpr uint32_t kItemNormalBoundsTopOffset = 0xC484;
constexpr float kPointOfCollectionY = 128.0f;
constexpr uint32_t kFocusInput = 0x08;
constexpr int32_t kEnemyDeadPlayerDamageDivisor = 5;

constexpr std::array<uintptr_t, 2> kRectCallSites{0x00409B42, 0x00409E47};
constexpr std::array<uintptr_t, 4> kCircleCallSites{
    0x00409B55, 0x00409E5A, 0x0041400E, 0x0041B544,
};
constexpr std::array<uintptr_t, 4> kRotatedCallSites{
    0x0041B5A5, 0x00429A12, 0x0042B024, 0x0042CBB0,
};

std::array<RelativeCallPatch, kRectCallSites.size()> g_rectPatches{
    RelativeCallPatch(kRectCallSites[0], GameAddresses::kPlayerRectCollision),
    RelativeCallPatch(kRectCallSites[1], GameAddresses::kPlayerRectCollision),
};
std::array<RelativeCallPatch, kCircleCallSites.size()> g_circlePatches{
    RelativeCallPatch(kCircleCallSites[0], GameAddresses::kPlayerCircleCollision),
    RelativeCallPatch(kCircleCallSites[1], GameAddresses::kPlayerCircleCollision),
    RelativeCallPatch(kCircleCallSites[2], GameAddresses::kPlayerCircleCollision),
    RelativeCallPatch(kCircleCallSites[3], GameAddresses::kPlayerCircleCollision),
};
std::array<RelativeCallPatch, kRotatedCallSites.size()> g_rotatedPatches{
    RelativeCallPatch(kRotatedCallSites[0], GameAddresses::kPlayerRotatedCollision),
    RelativeCallPatch(kRotatedCallSites[1], GameAddresses::kPlayerRotatedCollision),
    RelativeCallPatch(kRotatedCallSites[2], GameAddresses::kPlayerRotatedCollision),
    RelativeCallPatch(kRotatedCallSites[3], GameAddresses::kPlayerRotatedCollision),
};
std::array<RelativeCallPatch, 2> g_damagePatches{
    RelativeCallPatch(GameAddresses::kPlayerDamageScanCall1,
                      GameAddresses::kPlayerDamageScan),
    RelativeCallPatch(GameAddresses::kPlayerDamageScanCall2,
                      GameAddresses::kPlayerDamageScan),
};
constexpr std::array<uintptr_t, 8> kGrazeCallSites{
    0x00409C78, 0x00409F65, 0x00414043, 0x0041B579,
    0x0041B5DA, 0x00429A71, 0x0042B084, 0x0042CC13,
};
std::array<RelativeCallPatch, kGrazeCallSites.size()> g_grazePatches{
    RelativeCallPatch(kGrazeCallSites[0], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[1], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[2], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[3], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[4], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[5], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[6], GameAddresses::kPlayerGrazeSettlement),
    RelativeCallPatch(kGrazeCallSites[7], GameAddresses::kPlayerGrazeSettlement),
};

CodePatch g_itemBeginPatch;
CodePatch g_itemAdvancePatch;
CodePatch g_aimTargetPatch;
Phase9Counters g_counters;
std::wstring g_root;
bool g_installed = false;
bool g_eventLog = true;
bool g_loggedP2Damage = false;
bool g_loggedP2Hit = false;
bool g_loggedP2Graze = false;
bool g_loggedP2Item = false;
bool g_loggedP2ItemAttraction = false;
bool g_loggedP2Aim = false;
bool g_loggedAliveShotPool = false;
bool g_loggedEliminatedShotPool = false;
bool g_loggedAliveShotDamage = false;
bool g_loggedEliminatedShotDamage = false;
bool g_loggedDeadPlayerDamageCompensation = false;

void* g_itemOriginalPlayer = nullptr;
void* g_itemOriginalAnmArchive = nullptr;
void* g_itemCurrent = nullptr;
uint32_t g_itemOriginalState = 0;
uint32_t g_itemOwner = 0;
bool g_itemOwnerContact = false;
bool g_itemAirframeContextSwitched = false;
int32_t g_itemOriginalCharacter = 0;
int32_t g_itemOriginalShotType = 0;
uint32_t g_itemOriginalInput = 0;
uint32_t g_itemOwnerInput = 0;
uint32_t g_pendingGrazeMask = 0;

using GrazeFunction = void (__stdcall*)(void*);

void DispatchPendingGrazes(void* nativePosition) noexcept {
    const uint32_t mask = g_pendingGrazeMask;
    g_pendingGrazeMask = 0;
    if (mask == 0) {
        reinterpret_cast<GrazeFunction>(GameAddresses::kPlayerGrazeSettlement)(
            nativePosition);
        return;
    }
    auto* singleton = reinterpret_cast<void* volatile*>(
        GameAddresses::kPlayerSingleton);
    void* player1 = *singleton;
    if ((mask & 1U) != 0 && player1 != nullptr) {
        reinterpret_cast<GrazeFunction>(GameAddresses::kPlayerGrazeSettlement)(
            static_cast<uint8_t*>(player1) + kPositionXOffset);
    }
    void* player2 = RuntimePlayer2();
    if ((mask & 2U) != 0 && player2 != nullptr) {
        PlayerContextManager* contexts = RuntimePlayerContexts();
        if (contexts != nullptr &&
            contexts->ActiveSlot() == PlayerSlot::kPlayer1) {
            ScopedPlayerContext scope(*contexts, PlayerSlot::kPlayer2);
            if (scope.IsActive()) {
                reinterpret_cast<GrazeFunction>(
                    GameAddresses::kPlayerGrazeSettlement)(
                        static_cast<uint8_t*>(player2) + kPositionXOffset);
            }
        }
    }
}

constexpr uint8_t kUnassignedItemOwner = 0xFF;
constexpr size_t kItemOwnerRecordCount = 4096;

struct ItemOwnerRecord {
    void* item{};
    uint8_t owner{kUnassignedItemOwner};
};

std::array<ItemOwnerRecord, kItemOwnerRecordCount> g_itemOwnerRecords{};

template <typename T>
T& Field(void* object, uint32_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

struct ShotPoolSummary {
    uint32_t mainCount{};
    uint32_t optionCount{};
    int32_t mainDamage{};
    int32_t optionDamage{};
};

void Log(const std::wstring& message) noexcept;

ShotPoolSummary SummarizeShotPool(void* playerInf) noexcept {
    ShotPoolSummary summary{};
    if (playerInf == nullptr) {
        return summary;
    }
    auto* bullet = static_cast<uint8_t*>(playerInf) + kPlayerBulletPoolOffset;
    for (uint32_t index = 0; index < kPlayerBulletCapacity;
         ++index, bullet += kPlayerBulletStride) {
        const int32_t state = Field<int32_t>(bullet, kPlayerBulletStateOffset);
        if (state == 0 || state == 2) {
            continue;
        }
        void* record = Field<void*>(bullet, kPlayerBulletShotRecordOffset);
        const int32_t damage = Field<int32_t>(bullet, kPlayerBulletDamageOffset);
        const int8_t optionIndex = record != nullptr
            ? Field<int8_t>(record, kShotRecordOptionIndexOffset)
            : 0;
        if (optionIndex == 0) {
            ++summary.mainCount;
            summary.mainDamage += damage;
        } else {
            ++summary.optionCount;
            summary.optionDamage += damage;
        }
    }
    return summary;
}

void LogShotPoolComparison(bool eliminated, void* player2,
                           const ShotPoolSummary& summary,
                           int32_t scanDamage) noexcept {
    std::wstringstream message;
    message << L"phase11 P2 shot diagnostic; p1="
            << (eliminated ? L"eliminated" : L"alive")
            << L"; power="
            << *reinterpret_cast<volatile int32_t*>(GameAddresses::kPower)
            << L"; state=" << Field<int32_t>(player2, kPlayerStateOffset)
            << L"; state_timer="
            << Field<int32_t>(player2, kPlayerStateTimerPreviousOffset)
            << L"/"
            << Field<int32_t>(player2, kPlayerStateTimerCurrentOffset)
            << L"; sht=0x" << std::hex
            << reinterpret_cast<uintptr_t>(Field<void*>(
                   player2, GameAddresses::kPlayerShtPointerOffset))
            << std::dec << L"; main=" << summary.mainCount << L"/"
            << summary.mainDamage << L"; option=" << summary.optionCount
            << L"/" << summary.optionDamage << L"; scan_damage="
            << scanDamage;
    Log(message.str());
}

void Log(const std::wstring& message) noexcept {
    if (!g_root.empty()) {
        coop::WriteLog(g_root, L"patch.log", message);
    }
}

bool BuildRelativeCall(uintptr_t site, void* replacement, size_t size,
                       std::vector<uint8_t>& bytes) noexcept {
    if (size < 5 || replacement == nullptr) {
        return false;
    }
    const intptr_t relative = reinterpret_cast<uintptr_t>(replacement) - (site + 5);
    if (relative < INT32_MIN || relative > INT32_MAX) {
        return false;
    }
    bytes.assign(size, 0x90);
    bytes[0] = 0xE8;
    const int32_t displacement = static_cast<int32_t>(relative);
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return true;
}

void RestoreHooks() noexcept {
    g_aimTargetPatch.Restore();
    g_itemAdvancePatch.Restore();
    g_itemBeginPatch.Restore();
    for (auto& patch : g_rotatedPatches) {
        patch.Restore();
    }
    for (auto& patch : g_circlePatches) {
        patch.Restore();
    }
    for (auto& patch : g_rectPatches) {
        patch.Restore();
    }
    for (auto& patch : g_damagePatches) {
        patch.Restore();
    }
    for (auto& patch : g_grazePatches) {
        patch.Restore();
    }
    g_installed = false;
}

bool InstallHooks(std::wstring& error) noexcept {
    for (auto& patch : g_grazePatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase9GrazeSettlementHook))) {
            RestoreHooks();
            error = L"phase 9 graze-settlement call-site signature mismatch";
            return false;
        }
    }
    for (auto& patch : g_damagePatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase9PlayerDamageHook))) {
            RestoreHooks();
            error = L"phase 9 player-damage call-site signature mismatch";
            return false;
        }
    }
    for (auto& patch : g_rectPatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase9RectCollisionHook))) {
            RestoreHooks();
            error = L"phase 9 rectangle-collision call-site signature mismatch";
            return false;
        }
    }
    for (auto& patch : g_circlePatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase9CircleCollisionHook))) {
            RestoreHooks();
            error = L"phase 9 circle-collision call-site signature mismatch";
            return false;
        }
    }
    for (auto& patch : g_rotatedPatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase9RotatedCollisionHook))) {
            RestoreHooks();
            error = L"phase 9 rotated-collision call-site signature mismatch";
            return false;
        }
    }

    std::vector<uint8_t> itemBegin;
    std::vector<uint8_t> itemAdvance;
    std::vector<uint8_t> aimTarget;
    if (!BuildRelativeCall(GameAddresses::kItemLoopStateLoad,
                           reinterpret_cast<void*>(&Phase9ItemBeginHook), 6,
                           itemBegin) ||
        !BuildRelativeCall(GameAddresses::kItemLoopAdvance,
                           reinterpret_cast<void*>(&Phase9ItemAdvanceHook), 5,
                           itemAdvance) ||
        !BuildRelativeCall(GameAddresses::kAimPlayerLoad,
                           reinterpret_cast<void*>(&Phase9AimTargetHook), 5,
                           aimTarget)) {
        RestoreHooks();
        error = L"phase 9 item hook is outside relative-call range";
        return false;
    }
    g_itemBeginPatch = CodePatch(
        GameAddresses::kItemLoopStateLoad,
        {0x8B, 0x87, 0xB0, 0x09, 0x00, 0x00}, std::move(itemBegin));
    g_itemAdvancePatch = CodePatch(
        GameAddresses::kItemLoopAdvance,
        {0x8B, 0x44, 0x24, 0x10, 0x40}, std::move(itemAdvance));
    g_aimTargetPatch = CodePatch(
        GameAddresses::kAimPlayerLoad,
        {0xA1, 0x14, 0x45, 0x4B, 0x00}, std::move(aimTarget));
    if (!g_itemBeginPatch.Install() || !g_itemAdvancePatch.Install() ||
        !g_aimTargetPatch.Install()) {
        RestoreHooks();
        error = L"phase 9 item-contact code signature mismatch";
        return false;
    }
    g_installed = true;
    return true;
}

int CallRect(void* first, void* second) noexcept {
    int result = 0;
    const uintptr_t function = GameAddresses::kPlayerRectCollision;
    __asm {
        mov eax, first
        mov ecx, second
        call function
        mov result, eax
    }
    return result;
}

int CallCircle(void* first, void* second) noexcept {
    int result = 0;
    const uintptr_t function = GameAddresses::kPlayerCircleCollision;
    __asm {
        push second
        mov eax, first
        call function
        mov result, eax
    }
    return result;
}

int CallRotated(void* first, void* second, void* third, void* fourth) noexcept {
    int result = 0;
    const uintptr_t function = GameAddresses::kPlayerRotatedCollision;
    __asm {
        push fourth
        push third
        push second
        mov eax, first
        call function
        mov result, eax
    }
    return result;
}

void CountCollision(uint32_t player, int result) noexcept {
    void* playerInf = player == 0
        ? *reinterpret_cast<void**>(GameAddresses::kPlayerSingleton)
        : RuntimePlayer2();
    const int32_t state = playerInf != nullptr
        ? Field<int32_t>(playerInf, kPlayerStateOffset)
        : -1;
    if (result == 1 && (player == 0 || state == 4)) {
        ++g_counters.hits[player];
    } else if (result == 2) {
        ++g_counters.grazes[player];
    }
    if (player == 1 && result == 1 && state == 4 && !g_loggedP2Hit &&
        g_eventLog) {
        Log(L"phase9 P2 hit attributed; state=4");
        g_loggedP2Hit = true;
    }
    if (player == 1 && result == 2 && !g_loggedP2Graze && g_eventLog) {
        Log(L"phase9 P2 graze attributed; provisional original settlement");
        g_loggedP2Graze = true;
    }
}

template <typename Call>
int DispatchCollision(Call&& call) noexcept {
    auto* singleton = reinterpret_cast<void* volatile*>(GameAddresses::kPlayerSingleton);
    void* original = *singleton;
    const int player1Result = call();
    CountCollision(0, player1Result);

    int player2Result = 0;
    void* player2 = RuntimePlayer2();
    if (player2 != nullptr && !RuntimeDeterminismMovementOnly()) {
        PlayerContextManager* contexts = RuntimePlayerContexts();
        if (contexts != nullptr &&
            contexts->ActiveSlot() == PlayerSlot::kPlayer1) {
            ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
            if (playerScope.IsActive()) {
                player2Result = call();
            }
        } else {
            *singleton = player2;
            player2Result = call();
            *singleton = original;
        }
        CountCollision(1, player2Result);
    }
    g_pendingGrazeMask = (player1Result == 2 ? 1U : 0U) |
                         (player2Result == 2 ? 2U : 0U);
    *singleton = original;
    return coop::NativeCollisionResult(player1Result, player2Result);
}

bool PointInsidePlayer(void* player, float x, float y) noexcept {
    if (player == nullptr || Field<int32_t>(player, kPlayerStateOffset) != 1) {
        return false;
    }
    const float x1 = Field<float>(player, kItemBoundsLeftOffset);
    const float y1 = Field<float>(player, kItemBoundsBottomOffset);
    const float x2 = Field<float>(player, kItemBoundsRightOffset);
    const float y2 = Field<float>(player, kItemBoundsTopOffset);
    return x >= (std::min)(x1, x2) && x <= (std::max)(x1, x2) &&
           y >= (std::min)(y1, y2) && y <= (std::max)(y1, y2);
}

bool PointInsideBounds(void* player, float x, float y, uint32_t leftOffset,
                       uint32_t bottomOffset, uint32_t rightOffset,
                       uint32_t topOffset) noexcept {
    if (player == nullptr || Field<int32_t>(player, kPlayerStateOffset) != 1) {
        return false;
    }
    const float x1 = Field<float>(player, leftOffset);
    const float y1 = Field<float>(player, bottomOffset);
    const float x2 = Field<float>(player, rightOffset);
    const float y2 = Field<float>(player, topOffset);
    return x >= (std::min)(x1, x2) && x <= (std::max)(x1, x2) &&
           y >= (std::min)(y1, y2) && y <= (std::max)(y1, y2);
}

bool ItemStateAllowsAutoCollect(uint32_t state) noexcept {
    return state != 3 && state != 4 && state != 6 && state != 7 && state != 8;
}

uint8_t ItemOwnerPriority(void* player, void* item, uint32_t input,
                          float x, float y) noexcept {
    if (player == nullptr || item == nullptr ||
        Field<int32_t>(player, kPlayerStateOffset) != 1) {
        return 0;
    }
    if (PointInsidePlayer(player, x, y)) {
        return 3;
    }

    const uint32_t itemState = Field<uint32_t>(item, kItemStateOffset);
    if (itemState == 6) {
        const uint32_t itemType = Field<uint32_t>(item, kItemTypeOffset);
        if (itemType >= 0x0D && itemType <= 0x0F) {
            const float dx = x - Field<float>(player, kPositionXOffset);
            const float dy = y - Field<float>(player, kPositionYOffset);
            if (dx * dx + dy * dy <= kUfoTokenControlDistanceSquared) {
                // Native state 6 chooses the player within 60 pixels whose
                // position drives the red/blue/green token colour cycle.
                return 2;
            }
        }
    }
    if (ItemStateAllowsAutoCollect(itemState)) {
        const bool focus = (input & kFocusInput) != 0;
        const bool insideAutoBounds = focus
            ? PointInsideBounds(player, x, y, kItemFocusBoundsLeftOffset,
                                kItemFocusBoundsBottomOffset,
                                kItemFocusBoundsRightOffset,
                                kItemFocusBoundsTopOffset)
            : PointInsideBounds(player, x, y, kItemNormalBoundsLeftOffset,
                                kItemNormalBoundsBottomOffset,
                                kItemNormalBoundsRightOffset,
                                kItemNormalBoundsTopOffset);
        if (insideAutoBounds) {
            return 2;
        }
    }

    if (itemState == 1 &&
        Field<float>(player, kPositionYOffset) < kPointOfCollectionY) {
        return 1;
    }
    return 0;
}

float DistanceSquared(void* player, float x, float y) noexcept {
    const float dx = x - Field<float>(player, kPositionXOffset);
    const float dy = y - Field<float>(player, kPositionYOffset);
    return dx * dx + dy * dy;
}

ItemOwnerRecord* FindItemOwnerRecord(void* item, bool create) noexcept {
    if (item == nullptr) {
        return nullptr;
    }
    size_t index =
        (reinterpret_cast<uintptr_t>(item) >> 4U) & (kItemOwnerRecordCount - 1);
    for (size_t probe = 0; probe < kItemOwnerRecordCount; ++probe) {
        ItemOwnerRecord& record =
            g_itemOwnerRecords[(index + probe) & (kItemOwnerRecordCount - 1)];
        if (record.item == item) {
            return &record;
        }
        if (record.item == nullptr) {
            if (!create) {
                return nullptr;
            }
            record.item = item;
            record.owner = kUnassignedItemOwner;
            return &record;
        }
    }
    return nullptr;
}

void ApplyItemOwner(void* owner, uint32_t input, uint32_t ownerIndex) noexcept {
    *reinterpret_cast<void* volatile*>(GameAddresses::kPlayerSingleton) = owner;
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask) = input;
    if (ownerIndex == 1) {
        DualResourceBanks* banks = RuntimeResourceBanks();
        const PlayerResourceBank* player2Bank =
            banks != nullptr ? banks->Player2() : nullptr;
        auto* anmManager = *reinterpret_cast<uint8_t**>(
            GameAddresses::kAnmManagerRoot);
        if (player2Bank != nullptr && anmManager != nullptr) {
            auto** anmSlot = reinterpret_cast<void**>(
                anmManager + GameAddresses::kAnmSlotTableOffset +
                GameAddresses::kPlayerAnmSlot * sizeof(void*));
            *reinterpret_cast<volatile int32_t*>(GameAddresses::kCharacter) =
                static_cast<int32_t>(player2Bank->airframeIndex / 2U);
            *reinterpret_cast<volatile int32_t*>(GameAddresses::kShotType) =
                static_cast<int32_t>(player2Bank->airframeIndex % 2U);
            *anmSlot = player2Bank->anmArchive;
            g_itemAirframeContextSwitched = true;
        }
    }
    g_itemOwner = ownerIndex;
    g_itemOwnerInput = input;
}

}  // namespace

bool InitializeRuntimeCollisions(const std::wstring& root,
                                 std::wstring& error) noexcept {
    if (g_installed) {
        return true;
    }
    g_root = root;
    const std::wstring configPath = root + L"\\coop\\config.ini";
    if (GetPrivateProfileIntW(L"phase9", L"enabled", 1,
                              configPath.c_str()) == 0) {
        Log(L"phase9 collision, damage and item ownership disabled by config");
        return true;
    }
    g_eventLog = GetPrivateProfileIntW(L"phase9", L"collision_log", 0,
                                       configPath.c_str()) != 0;
    g_counters = {};
    return InstallHooks(error);
}

const Phase9Counters& RuntimePhase9Counters() noexcept {
    return g_counters;
}

}  // namespace coop::th12

extern "C" {

int __stdcall Phase9PlayerDamageHook(void* position, void* hitbox) {
    const uintptr_t returnAddress =
        reinterpret_cast<uintptr_t>(_ReturnAddress());
    using DamageFunction = int (__stdcall*)(void*, void*);
    auto original = reinterpret_cast<DamageFunction>(
        coop::th12::GameAddresses::kPlayerDamageScan);
    auto* singleton = reinterpret_cast<void* volatile*>(
        coop::th12::GameAddresses::kPlayerSingleton);
    auto* bombGlobal = reinterpret_cast<void* volatile*>(
        coop::th12::GameAddresses::kBombManager);
    void* player1 = *singleton;
    void* player1Bomb = *bombGlobal;
    int player1Damage = 0;
    if (!coop::th12::RuntimePlayer1Eliminated()) {
        player1Damage = original(position, hitbox);
        ++coop::th12::g_counters.damageScans[0];
    }

    int player2Damage = 0;
    void* player2 = coop::th12::RuntimePlayer2();
    void* player2Bomb = coop::th12::RuntimePlayer2BombManager();
    const bool player1Eliminated =
        coop::th12::RuntimePlayer1Eliminated();
    const auto shotSummary = coop::th12::SummarizeShotPool(player2);
    coop::th12::PlayerContextManager* contexts =
        coop::th12::RuntimePlayerContexts();
    if (player2 != nullptr && contexts != nullptr &&
        contexts->ActiveSlot() == coop::th12::PlayerSlot::kPlayer1 &&
        !coop::th12::RuntimeDeterminismMovementOnly()) {
        coop::th12::ScopedPlayerContext playerScope(
            *contexts, coop::th12::PlayerSlot::kPlayer2);
        if (!playerScope.IsActive()) {
            return player1Damage;
        }
        *bombGlobal = player2Bomb != nullptr ? player2Bomb : player1Bomb;
        player2Damage = original(position, hitbox);
        *bombGlobal = player1Bomb;
        ++coop::th12::g_counters.damageScans[1];
        const bool hasShots = shotSummary.mainCount != 0 ||
                              shotSummary.optionCount != 0;
        bool& loggedPool = player1Eliminated
            ? coop::th12::g_loggedEliminatedShotPool
            : coop::th12::g_loggedAliveShotPool;
        if (hasShots && !loggedPool) {
            coop::th12::LogShotPoolComparison(
                player1Eliminated, player2, shotSummary, player2Damage);
            loggedPool = true;
        }
        bool& loggedDamage = player1Eliminated
            ? coop::th12::g_loggedEliminatedShotDamage
            : coop::th12::g_loggedAliveShotDamage;
        if (player2Damage > 0 && !loggedDamage) {
            coop::th12::LogShotPoolComparison(
                player1Eliminated, player2, shotSummary, player2Damage);
            loggedDamage = true;
        }
        if (player2Damage > 0 && !coop::th12::g_loggedP2Damage) {
            std::wstringstream message;
            message << L"phase10 P2 shot/Bomb damage observed; damage="
                    << player2Damage;
            coop::th12::Log(message.str());
            coop::th12::g_loggedP2Damage = true;
        }
    }
    *singleton = player1;
    // The ordinary-enemy caller at 0x413D90 divides the combined return value
    // by five whenever the global P1 state is entry/dead (0 or 2). That rule
    // predates P2 and would incorrectly reduce an active P2's damage as well.
    // Pre-scale only P2 here so the caller still applies the original reduction
    // to P1 while P2 emerges with its unmodified damage. The Boss caller has no
    // equivalent post-call division and must not receive this compensation.
    const bool ordinaryEnemyCall =
        returnAddress == coop::th12::GameAddresses::kPlayerDamageScanCall1 + 5U;
    const int32_t player1State = player1 != nullptr
        ? coop::th12::Field<int32_t>(player1,
                                    coop::th12::kPlayerStateOffset)
        : -1;
    const int32_t player2State = player2 != nullptr
        ? coop::th12::Field<int32_t>(player2,
                                    coop::th12::kPlayerStateOffset)
        : -1;
    int32_t combinedDamage = player1Damage + player2Damage;
    if (ordinaryEnemyCall) {
        combinedDamage = coop::ScaleEnemyDamagePerPlayer(
            player1Damage, player2Damage, player1State, player2State,
            coop::th12::kEnemyDeadPlayerDamageDivisor).returned;
    }
    if (ordinaryEnemyCall &&
        (player1State == 0 || player1State == 2) && player2State == 1) {
        combinedDamage = player1Damage +
            player2Damage * coop::th12::kEnemyDeadPlayerDamageDivisor;
        if (player2Damage > 0 &&
            !coop::th12::g_loggedDeadPlayerDamageCompensation) {
            std::wstringstream message;
            message << L"phase11 active P2 damage compensated against dead-P1 enemy scaling; p1="
                    << player1Damage << L"; p2=" << player2Damage
                    << L"; returned=" << combinedDamage;
            coop::th12::Log(message.str());
            coop::th12::g_loggedDeadPlayerDamageCompensation = true;
        }
    }
    return combinedDamage;
}

void __stdcall Phase9GrazeSettlementHook(void* position) {
    coop::th12::DispatchPendingGrazes(position);
}

int __stdcall Phase9DispatchRectCollision(void* first, void* second) {
    return coop::th12::DispatchCollision(
        [=]() noexcept { return coop::th12::CallRect(first, second); });
}

int __stdcall Phase9DispatchCircleCollision(void* first, void* second) {
    return coop::th12::DispatchCollision(
        [=]() noexcept { return coop::th12::CallCircle(first, second); });
}

int __stdcall Phase9DispatchRotatedCollision(void* first, void* second,
                                             void* third, void* fourth) {
    return coop::th12::DispatchCollision([=]() noexcept {
        return coop::th12::CallRotated(first, second, third, fourth);
    });
}

void* __stdcall Phase9SelectItemOwner(void* item) {
    auto* singleton = reinterpret_cast<void* volatile*>(
        coop::th12::GameAddresses::kPlayerSingleton);
    auto* globalInput = reinterpret_cast<volatile uint32_t*>(
        coop::th12::GameAddresses::kInputMask);
    auto* globalCharacter = reinterpret_cast<volatile int32_t*>(
        coop::th12::GameAddresses::kCharacter);
    auto* globalShotType = reinterpret_cast<volatile int32_t*>(
        coop::th12::GameAddresses::kShotType);
    if (coop::th12::RuntimeResourceTransactionsEnabled()) {
        if (!coop::th12::RuntimeResourceFrameActive()) {
            coop::th12::BeginRuntimeResourceFrame();
        } else {
            // Preserve native UFO state-machine transitions that may have run
            // between scheduler callbacks before exposing queued item events.
            coop::th12::CaptureRuntimeNativeResourceChanges(2);
        }
        coop::th12::ExposeRuntimeResourceProjection();
    }

    void* player1 = *singleton;
    coop::th12::g_itemOriginalPlayer = player1;
    coop::th12::g_itemOriginalCharacter = *globalCharacter;
    coop::th12::g_itemOriginalShotType = *globalShotType;
    coop::th12::g_itemOriginalAnmArchive = nullptr;
    coop::th12::g_itemAirframeContextSwitched = false;
    auto* anmManager = *reinterpret_cast<uint8_t**>(
        coop::th12::GameAddresses::kAnmManagerRoot);
    if (anmManager != nullptr) {
        auto** anmSlot = reinterpret_cast<void**>(
            anmManager + coop::th12::GameAddresses::kAnmSlotTableOffset +
            coop::th12::GameAddresses::kPlayerAnmSlot * sizeof(void*));
        coop::th12::g_itemOriginalAnmArchive = *anmSlot;
    }
    coop::th12::g_itemCurrent = item;
    coop::th12::g_itemOriginalState = item != nullptr
        ? coop::th12::Field<uint32_t>(item,
                                     coop::th12::kItemStateOffset)
        : 0;
    coop::th12::g_itemOwner = 0;
    coop::th12::g_itemOwnerContact = false;
    coop::th12::g_itemOriginalInput = *globalInput;
    coop::th12::g_itemOwnerInput = coop::th12::g_itemOriginalInput;

    coop::th12::ItemOwnerRecord* ownerRecord =
        coop::th12::FindItemOwnerRecord(
            item, coop::th12::g_itemOriginalState != 0);

    void* player2 = coop::th12::RuntimePlayer2();
    if (item == nullptr || player1 == nullptr || player2 == nullptr ||
        coop::th12::RuntimeDeterminismMovementOnly()) {
        return player1;
    }
    coop::th12::PlayerContextManager* contexts =
        coop::th12::RuntimePlayerContexts();
    const coop::th12::PlayerContext* player2Context = contexts != nullptr
        ? contexts->GetContext(coop::th12::PlayerSlot::kPlayer2)
        : nullptr;
    if (player2Context == nullptr) {
        return player1;
    }

    const uint32_t player2Input = player2Context->inputMask;
    const uint32_t itemState = coop::th12::g_itemOriginalState;
    if ((itemState == 3 || itemState == 4) && ownerRecord != nullptr &&
        ownerRecord->owner != coop::th12::kUnassignedItemOwner) {
        if (ownerRecord->owner == 1 &&
            coop::th12::Field<int32_t>(player2,
                                      coop::th12::kPlayerStateOffset) == 1) {
            coop::th12::ApplyItemOwner(player2, player2Input, 1);
            const float x = coop::th12::Field<float>(
                item, coop::th12::kItemPositionXOffset);
            const float y = coop::th12::Field<float>(
                item, coop::th12::kItemPositionYOffset);
            coop::th12::g_itemOwnerContact =
                coop::th12::PointInsidePlayer(player2, x, y);
            if (!coop::th12::g_loggedP2ItemAttraction) {
                coop::th12::Log(
                    L"phase9 P2 item attraction target retained");
                coop::th12::g_loggedP2ItemAttraction = true;
            }
            return player2;
        }
        ownerRecord->owner = 0;
        return player1;
    }

    const float x = coop::th12::Field<float>(
        item, coop::th12::kItemPositionXOffset);
    const float y = coop::th12::Field<float>(
        item, coop::th12::kItemPositionYOffset);
    const uint8_t player1Priority = coop::th12::ItemOwnerPriority(
        player1, item, coop::th12::g_itemOriginalInput, x, y);
    const uint8_t player2Priority = coop::th12::ItemOwnerPriority(
        player2, item, player2Input, x, y);
    if (player1Priority == 0 && player2Priority == 0) {
        if (ownerRecord != nullptr && itemState != 3 && itemState != 4) {
            ownerRecord->owner = coop::th12::kUnassignedItemOwner;
        }
        return player1;
    }
    const coop::ContactOwner resolved = coop::ResolveItemOwnerPriority(
        player1Priority, player2Priority,
        coop::th12::DistanceSquared(player1, x, y),
        coop::th12::DistanceSquared(player2, x, y));
    void* owner = player1;
    if (resolved == coop::ContactOwner::kPlayer2) {
        owner = player2;
        coop::th12::ApplyItemOwner(player2, player2Input, 1);
        if (player2Priority < 3 &&
            !coop::th12::g_loggedP2ItemAttraction) {
            coop::th12::Log(L"phase9 P2 item attraction owner selected");
            coop::th12::g_loggedP2ItemAttraction = true;
        }
    } else {
        coop::th12::ApplyItemOwner(
            player1, coop::th12::g_itemOriginalInput, 0);
    }
    if (ownerRecord != nullptr) {
        ownerRecord->owner = resolved == coop::ContactOwner::kPlayer2 ? 1 : 0;
    }
    coop::th12::g_itemOwnerContact =
        coop::th12::PointInsidePlayer(owner, x, y);
    return owner;
}

void __stdcall Phase9FinishItemSlot() {
    auto* singleton = reinterpret_cast<void* volatile*>(
        coop::th12::GameAddresses::kPlayerSingleton);
    auto* globalInput = reinterpret_cast<volatile uint32_t*>(
        coop::th12::GameAddresses::kInputMask);
    auto* globalCharacter = reinterpret_cast<volatile int32_t*>(
        coop::th12::GameAddresses::kCharacter);
    auto* globalShotType = reinterpret_cast<volatile int32_t*>(
        coop::th12::GameAddresses::kShotType);
    const uint32_t finalState = coop::th12::g_itemCurrent != nullptr
        ? coop::th12::Field<uint32_t>(coop::th12::g_itemCurrent,
                                     coop::th12::kItemStateOffset)
        : 0;
    coop::th12::CaptureRuntimeNativeResourceChanges(
        static_cast<uint8_t>(coop::th12::g_itemOwner));
    if (coop::th12::g_itemCurrent != nullptr &&
        coop::th12::g_itemOwnerContact &&
        coop::th12::g_itemOriginalState != 0 &&
        finalState == 0) {
        ++coop::th12::g_counters.itemContacts[coop::th12::g_itemOwner];
        if (coop::th12::g_itemOwner == 1 && !coop::th12::g_loggedP2Item) {
            coop::th12::Log(
                L"phase9 P2 item contact attributed; shared settlement deferred");
            coop::th12::g_loggedP2Item = true;
        }
    }
    if (coop::th12::g_itemOriginalPlayer != nullptr) {
        *singleton = coop::th12::g_itemOriginalPlayer;
        *globalInput = coop::th12::g_itemOriginalInput;
        *globalCharacter = coop::th12::g_itemOriginalCharacter;
        *globalShotType = coop::th12::g_itemOriginalShotType;
        if (coop::th12::g_itemAirframeContextSwitched) {
            auto* anmManager = *reinterpret_cast<uint8_t**>(
                coop::th12::GameAddresses::kAnmManagerRoot);
            if (anmManager != nullptr) {
                auto** anmSlot = reinterpret_cast<void**>(
                    anmManager +
                    coop::th12::GameAddresses::kAnmSlotTableOffset +
                    coop::th12::GameAddresses::kPlayerAnmSlot * sizeof(void*));
                *anmSlot = coop::th12::g_itemOriginalAnmArchive;
            }
        }
    }
    if (finalState == 0 && coop::th12::g_itemCurrent != nullptr) {
        coop::th12::ItemOwnerRecord* ownerRecord =
            coop::th12::FindItemOwnerRecord(coop::th12::g_itemCurrent, false);
        if (ownerRecord != nullptr) {
            ownerRecord->owner = coop::th12::kUnassignedItemOwner;
        }
    }
    coop::th12::g_itemOriginalPlayer = nullptr;
    coop::th12::g_itemOriginalAnmArchive = nullptr;
    coop::th12::g_itemCurrent = nullptr;
    coop::th12::g_itemOwnerContact = false;
    coop::th12::g_itemAirframeContextSwitched = false;
    coop::th12::g_itemOriginalInput = 0;
    coop::th12::g_itemOwnerInput = 0;
}

void* __stdcall Phase9SelectAimTarget(void* sourcePosition) {
    void* player1 = *reinterpret_cast<void* volatile*>(
        coop::th12::GameAddresses::kPlayerSingleton);
    if (coop::th12::g_itemCurrent != nullptr || sourcePosition == nullptr ||
        coop::th12::RuntimeDeterminismMovementOnly()) {
        return player1;
    }

    void* player2 = coop::th12::RuntimePlayer2();
    if (player2 == nullptr ||
        coop::th12::Field<int32_t>(player2,
                                  coop::th12::kPlayerStateOffset) != 1) {
        return player1;
    }
    if (player1 == nullptr ||
        coop::th12::Field<int32_t>(player1,
                                  coop::th12::kPlayerStateOffset) != 1) {
        return player2;
    }

    const float x = *static_cast<float*>(sourcePosition);
    const float y = *(static_cast<float*>(sourcePosition) + 1);
    if (coop::th12::DistanceSquared(player2, x, y) <
        coop::th12::DistanceSquared(player1, x, y)) {
        if (!coop::th12::g_loggedP2Aim) {
            coop::th12::Log(L"phase9 nearest-player aim selected P2");
            coop::th12::g_loggedP2Aim = true;
        }
        return player2;
    }
    return player1;
}

}  // extern "C"
