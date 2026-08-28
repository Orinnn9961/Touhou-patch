#include "runtime_resources.h"

#include "log.h"
#include "memory_patch.h"
#include "runtime_frame_input.h"
#include "runtime_player2.h"
#include "version_map.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>

namespace coop::th12 {
void OnPlayerInitialized(void* playerInf) noexcept;
void BeforePlayerInitialize() noexcept;
void OnPlayerDestroying() noexcept;
}  // namespace coop::th12

extern "C" {

uintptr_t g_originalPlayerInitialize = coop::th12::GameAddresses::kPlayerInitialize;

void __stdcall Phase4AfterPlayerInitialize(void* playerInf) {
    coop::th12::OnPlayerInitialized(playerInf);
}

void __stdcall Phase14BeforePlayerInitialize() {
    coop::th12::BeforePlayerInitialize();
}

__declspec(naked) void Phase4PlayerInitializeHook() {
    __asm {
        call Phase14BeforePlayerInitialize
        call dword ptr [g_originalPlayerInitialize]
        test eax, eax
        jne initialization_finished
        push eax
        push edi
        call Phase4AfterPlayerInitialize
        pop eax
    initialization_finished:
        ret
    }
}

void __stdcall Phase4PlayerDestroyHook(void* playerInf) {
    coop::th12::OnPlayerDestroying();
    using PlayerDestroy = void (__stdcall*)(void*);
    reinterpret_cast<PlayerDestroy>(coop::th12::GameAddresses::kPlayerDestroy)(playerInf);
}

}  // extern "C"

namespace coop::th12 {
namespace {

constexpr std::array<uintptr_t, 5> kPlayerDestroyCallSites{
    0x0040EC97, 0x00422436, 0x00436473, 0x004364B1, 0x004364DC,
};

RelativeCallPatch g_initializePatch(GameAddresses::kPlayerInitializeCallSite,
                                    GameAddresses::kPlayerInitialize);
std::array<RelativeCallPatch, kPlayerDestroyCallSites.size()> g_destroyPatches{
    RelativeCallPatch(kPlayerDestroyCallSites[0], GameAddresses::kPlayerDestroy),
    RelativeCallPatch(kPlayerDestroyCallSites[1], GameAddresses::kPlayerDestroy),
    RelativeCallPatch(kPlayerDestroyCallSites[2], GameAddresses::kPlayerDestroy),
    RelativeCallPatch(kPlayerDestroyCallSites[3], GameAddresses::kPlayerDestroy),
    RelativeCallPatch(kPlayerDestroyCallSites[4], GameAddresses::kPlayerDestroy),
};

DualResourceBanks g_resourceBanks;
std::wstring g_root;
uint32_t g_player2Airframe = 5;
bool g_hooksInstalled = false;

template <typename T>
T& PlayerField(void* playerInf, uint32_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(playerInf) + offset);
}

std::wstring Widen(const char* value) {
    std::wstring result;
    while (*value != '\0') {
        result.push_back(static_cast<unsigned char>(*value));
        ++value;
    }
    return result;
}

void Log(const std::wstring& message) noexcept {
    if (!g_root.empty()) {
        coop::WriteLog(g_root, L"patch.log", message);
    }
}

void* CallAnmLoad(uint32_t slot, const char* filename) noexcept {
    void* result = nullptr;
    const uintptr_t function = GameAddresses::kAnmLoad;
    __asm {
        mov ecx, slot
        mov ebx, filename
        call function
        mov result, eax
    }
    return result;
}

int CallShtLoad(void* scratchPlayer, const char* filename) noexcept {
    int result = -1;
    const uintptr_t function = GameAddresses::kShtLoadAndRelocate;
    __asm {
        mov esi, scratchPlayer
        mov eax, filename
        call function
        mov result, eax
    }
    return result;
}

void GameFree(void* allocation) noexcept {
    if (allocation == nullptr) {
        return;
    }
    using FreeFunction = void (__cdecl*)(void*);
    reinterpret_cast<FreeFunction>(GameAddresses::kGameFree)(allocation);
}

void DestroyDetachedAnm(void* archive) noexcept {
    if (archive == nullptr) {
        return;
    }
    const uintptr_t function = GameAddresses::kAnmDestroyArchive;
    __asm {
        mov edi, archive
        call function
    }
    GameFree(archive);
}

void** PlayerAnmSlot() noexcept {
    const auto manager = *reinterpret_cast<uint8_t**>(GameAddresses::kAnmManagerRoot);
    if (manager == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void**>(manager + GameAddresses::kAnmSlotTableOffset +
                                    GameAddresses::kPlayerAnmSlot * sizeof(void*));
}

class ScopedAnmSlotRestore {
public:
    ScopedAnmSlotRestore(void** slot, void* original) noexcept
        : slot_(slot), original_(original) {
        *slot_ = nullptr;
    }

    ~ScopedAnmSlotRestore() {
        *slot_ = original_;
    }

    ScopedAnmSlotRestore(const ScopedAnmSlotRestore&) = delete;
    ScopedAnmSlotRestore& operator=(const ScopedAnmSlotRestore&) = delete;

private:
    void** slot_;
    void* original_;
};

void* LoadDetachedAnm(void* player1Archive, const char* filename,
                      std::wstring& error) noexcept {
    void** slot = PlayerAnmSlot();
    if (slot == nullptr || *slot != player1Archive) {
        error = L"player ANM slot 7 does not match PlayerInf archive";
        return nullptr;
    }

    void* loaded = nullptr;
    void* registered = nullptr;
    {
        ScopedAnmSlotRestore restore(slot, player1Archive);
        loaded = CallAnmLoad(GameAddresses::kPlayerAnmSlot, filename);
        registered = *slot;
    }
    if (loaded == nullptr || registered != loaded || loaded == player1Archive) {
        if (loaded != nullptr && loaded != registered && loaded != player1Archive) {
            DestroyDetachedAnm(loaded);
        }
        if (registered != nullptr && registered != player1Archive) {
            DestroyDetachedAnm(registered);
        }
        error = L"unable to load detached P2 ANM archive";
        return nullptr;
    }
    return loaded;
}

void* LoadIndependentSht(const char* filename, std::wstring& error) noexcept {
    constexpr size_t kScratchSize = GameAddresses::kPlayerShtPointerOffset + sizeof(void*);
    alignas(void*) std::array<uint8_t, kScratchSize> scratch{};
    if (CallShtLoad(scratch.data(), filename) != 0) {
        error = L"unable to load P2 SHT";
        return nullptr;
    }
    void* result = *reinterpret_cast<void**>(
        scratch.data() + GameAddresses::kPlayerShtPointerOffset);
    if (result == nullptr) {
        error = L"P2 SHT loader returned an empty allocation";
    }
    return result;
}

bool ParsePlayer2Airframe(const std::wstring& value, uint32_t& index) {
    for (uint32_t i = 0; i < kAirframes.size(); ++i) {
        if (_wcsicmp(value.c_str(), Widen(kAirframes[i].id).c_str()) == 0) {
            index = i;
            return true;
        }
    }
    return false;
}

void RestoreHooks() noexcept {
    g_initializePatch.Restore();
    for (auto& patch : g_destroyPatches) {
        patch.Restore();
    }
    g_hooksInstalled = false;
}

bool InstallHooks(std::wstring& error) noexcept {
    for (auto& patch : g_destroyPatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase4PlayerDestroyHook))) {
            RestoreHooks();
            error = L"phase 4 Player destroy call-site signature mismatch";
            return false;
        }
    }
    if (!g_initializePatch.Install(reinterpret_cast<void*>(&Phase4PlayerInitializeHook))) {
        RestoreHooks();
        error = L"phase 4 Player initialize call-site signature mismatch";
        return false;
    }
    g_hooksInstalled = true;
    return true;
}

}  // namespace

bool InitializeRuntimeResources(const std::wstring& root, std::wstring& error) noexcept {
    if (g_hooksInstalled) {
        return true;
    }
    g_root = root;
    const std::wstring configPath = root + L"\\coop\\config.ini";
    if (GetPrivateProfileIntW(L"phase4", L"enabled", 1, configPath.c_str()) == 0) {
        Log(L"phase4 dual resource preload disabled by config");
        return true;
    }
    wchar_t configured[32]{};
    GetPrivateProfileStringW(L"phase4", L"player2_airframe", L"sanae_b", configured,
                             static_cast<DWORD>(std::size(configured)), configPath.c_str());
    if (!ParsePlayer2Airframe(configured, g_player2Airframe)) {
        error = L"invalid phase4.player2_airframe";
        return false;
    }
    return InstallHooks(error);
}

void BeforePlayerInitialize() noexcept {
    const int32_t character =
        *reinterpret_cast<volatile int32_t*>(GameAddresses::kCharacter);
    const int32_t shotType =
        *reinterpret_cast<volatile int32_t*>(GameAddresses::kShotType);
    if (character < 0 || character > 2 || shotType < 0 || shotType > 1) {
        MarkRuntimeLobbyResourcesReady(false);
        return;
    }
    const uint32_t localSelection =
        static_cast<uint32_t>(shotType + character * 2);
    uint32_t player1Airframe = 0;
    uint32_t player2Airframe = 0;
    if (!CoordinateRuntimeAirframes(localSelection, player1Airframe,
                                    player2Airframe)) {
        return;
    }
    g_player2Airframe = player2Airframe;
    *reinterpret_cast<volatile int32_t*>(GameAddresses::kCharacter) =
        static_cast<int32_t>(player1Airframe / 2);
    *reinterpret_cast<volatile int32_t*>(GameAddresses::kShotType) =
        static_cast<int32_t>(player1Airframe % 2);
}

DualResourceBanks* RuntimeResourceBanks() noexcept {
    return g_hooksInstalled ? &g_resourceBanks : nullptr;
}

bool BindPlayer2Resources(void* playerInf) noexcept {
    const PlayerResourceBank* bank = g_resourceBanks.Player2();
    if (playerInf == nullptr || bank == nullptr || !bank->IsReady()) {
        return false;
    }
    PlayerField<void*>(playerInf, GameAddresses::kPlayerAnmPointerOffset) = bank->anmArchive;
    PlayerField<void*>(playerInf, GameAddresses::kPlayerShtPointerOffset) = bank->shtData;
    return true;
}

void ReleaseRuntimePlayer2Resources() noexcept {
    const PlayerResourceBank* bank = g_resourceBanks.Player2();
    if (bank == nullptr) {
        g_resourceBanks.Clear();
        return;
    }
    const PlayerResourceBank owned = *bank;
    g_resourceBanks.Clear();
    if (owned.ownsShtData) {
        GameFree(owned.shtData);
    }
    if (owned.ownsAnmArchive) {
        DestroyDetachedAnm(owned.anmArchive);
    }
}

void OnPlayerInitialized(void* playerInf) noexcept {
    const auto lobbyFailure = []() noexcept {
        if (RuntimeNetworkActive()) {
            MarkRuntimeLobbyResourcesReady(false);
        }
    };
    DestroyRuntimePlayer2();
    ReleaseRuntimePlayer2Resources();
    if (playerInf == nullptr) {
        lobbyFailure();
        return;
    }

    const int32_t character = *reinterpret_cast<volatile int32_t*>(GameAddresses::kCharacter);
    const int32_t shotType = *reinterpret_cast<volatile int32_t*>(GameAddresses::kShotType);
    if (character < 0 || character > 2 || shotType < 0 || shotType > 1) {
        Log(L"phase4 skipped: invalid P1 airframe selection");
        lobbyFailure();
        return;
    }
    const uint32_t player1Airframe = static_cast<uint32_t>(shotType + character * 2);
    ResourceLoadPlan plan;
    if (!BuildResourceLoadPlan(player1Airframe, g_player2Airframe, plan)) {
        Log(L"phase4 skipped: unable to build resource plan");
        lobbyFailure();
        return;
    }

    void* player1Anm = PlayerField<void*>(playerInf, GameAddresses::kPlayerAnmPointerOffset);
    void* player1Sht = PlayerField<void*>(playerInf, GameAddresses::kPlayerShtPointerOffset);
    if (!g_resourceBanks.SetPlayer1(
            {player1Airframe, player1Anm, player1Sht, false, false})) {
        Log(L"phase4 skipped: P1 resources are incomplete");
        lobbyFailure();
        return;
    }

    std::wstring error;
    void* player2Anm = player1Anm;
    bool ownsAnm = false;
    if (!plan.shareAnmArchive) {
        player2Anm = LoadDetachedAnm(player1Anm, kAirframes[g_player2Airframe].anmFile,
                                     error);
        ownsAnm = player2Anm != nullptr;
    }
    if (player2Anm == nullptr) {
        g_resourceBanks.Clear();
        Log(L"phase4 P2 resource load failed: " + error);
        lobbyFailure();
        return;
    }

    void* player2Sht = LoadIndependentSht(kAirframes[g_player2Airframe].shtFile, error);
    if (player2Sht == nullptr) {
        if (ownsAnm) {
            DestroyDetachedAnm(player2Anm);
        }
        g_resourceBanks.Clear();
        Log(L"phase4 P2 resource load failed: " + error);
        lobbyFailure();
        return;
    }
    if (!g_resourceBanks.SetPlayer2(
            {g_player2Airframe, player2Anm, player2Sht, ownsAnm, true})) {
        GameFree(player2Sht);
        if (ownsAnm) {
            DestroyDetachedAnm(player2Anm);
        }
        g_resourceBanks.Clear();
        Log(L"phase4 rejected a colliding dual-resource layout");
        lobbyFailure();
        return;
    }

    std::wstringstream message;
    message << L"phase4 resources resident; p1=" << Widen(kAirframes[player1Airframe].id)
            << L"; p2=" << Widen(kAirframes[g_player2Airframe].id)
            << L"; anm=" << (plan.shareAnmArchive ? L"shared-character" : L"isolated")
            << L"; sht=isolated";
    Log(message.str());
    const bool player2Created = CreateRuntimePlayer2();
    MarkRuntimeLobbyResourcesReady(player2Created);
}

void OnPlayerDestroying() noexcept {
    DestroyRuntimePlayer2();
    if (g_resourceBanks.Player2() != nullptr) {
        Log(L"phase4 releasing P2 resource bank before Player destroy");
    }
    ReleaseRuntimePlayer2Resources();
}

}  // namespace coop::th12
