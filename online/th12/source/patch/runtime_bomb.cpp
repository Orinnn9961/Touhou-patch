#include "runtime_bomb.h"

#include "log.h"
#include "player2_input.h"
#include "runtime_determinism.h"
#include "runtime_player2.h"
#include "runtime_resource_tx.h"
#include "runtime_player_context.h"
#include "runtime_resources.h"
#include "version_map.h"

#include <windows.h>

#include <cstdint>
#include <sstream>

namespace coop::th12 {
namespace {

constexpr uint32_t kUpdateRegistrationOffset = 0x08;
constexpr uint32_t kRenderRegistrationOffset = 0x0C;
constexpr uint32_t kRegistrationFlagsOffset = 0x04;
constexpr uint32_t kRegistrationEnabledFlag = 0x02;
constexpr uint32_t kBombActiveOffset = 0x3C;
constexpr uint32_t kBombVm40Offset = 0x40;
constexpr uint32_t kBombVm44Offset = 0x44;
constexpr uint32_t kBombVm48Offset = 0x48;
constexpr uint32_t kPlayerStateOffset = 0xA28;

std::wstring g_root;
void* g_player2Bomb = nullptr;
bool g_configured = false;
bool g_enabled = false;
bool g_loggedStart = false;
bool g_loggedEnd = false;

template <typename T>
T& Field(void* object, uint32_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void ClearCompletedBombVmHandles(void* bombManager) noexcept {
    Field<uint32_t>(bombManager, kBombVm40Offset) = 0;
    Field<uint32_t>(bombManager, kBombVm44Offset) = 0;
    Field<uint32_t>(bombManager, kBombVm48Offset) = 0;
}

void Log(const std::wstring& message) noexcept {
    if (!g_root.empty()) {
        coop::WriteLog(g_root, L"patch.log", message);
    }
}

void** BombGlobal() noexcept {
    return reinterpret_cast<void**>(GameAddresses::kBombManager);
}

void** PlayerAnmSlot() noexcept {
    const auto manager =
        *reinterpret_cast<uint8_t**>(GameAddresses::kAnmManagerRoot);
    if (manager == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void**>(
        manager + GameAddresses::kAnmSlotTableOffset +
        GameAddresses::kPlayerAnmSlot * sizeof(void*));
}

class ScopedPointerValue {
public:
    ScopedPointerValue(void** target, void* temporary) noexcept
        : target_(target), original_(*target) {
        *target_ = temporary;
    }

    ~ScopedPointerValue() {
        *target_ = original_;
    }

    ScopedPointerValue(const ScopedPointerValue&) = delete;
    ScopedPointerValue& operator=(const ScopedPointerValue&) = delete;

private:
    void** target_;
    void* original_;
};

class ScopedAirframeGlobals {
public:
    explicit ScopedAirframeGlobals(uint32_t airframe) noexcept
        : character_(reinterpret_cast<volatile int32_t*>(
              GameAddresses::kCharacter)),
          shotType_(reinterpret_cast<volatile int32_t*>(
              GameAddresses::kShotType)),
          originalCharacter_(*character_), originalShotType_(*shotType_) {
        *character_ = static_cast<int32_t>(airframe / 2U);
        *shotType_ = static_cast<int32_t>(airframe % 2U);
    }

    ~ScopedAirframeGlobals() {
        *character_ = originalCharacter_;
        *shotType_ = originalShotType_;
    }

    ScopedAirframeGlobals(const ScopedAirframeGlobals&) = delete;
    ScopedAirframeGlobals& operator=(const ScopedAirframeGlobals&) = delete;

private:
    volatile int32_t* character_;
    volatile int32_t* shotType_;
    int32_t originalCharacter_;
    int32_t originalShotType_;
};

void SetRegistrationEnabled(void* registration, bool enabled) noexcept {
    if (registration == nullptr) {
        return;
    }
    auto& flags = Field<uint32_t>(registration, kRegistrationFlagsOffset);
    if (enabled) {
        flags |= kRegistrationEnabledFlag;
    } else {
        flags &= ~kRegistrationEnabledFlag;
    }
}

void DisableAutomaticCallbacks(void* bombManager) noexcept {
    if (bombManager == nullptr) {
        return;
    }
    SetRegistrationEnabled(
        Field<void*>(bombManager, kUpdateRegistrationOffset), false);
    SetRegistrationEnabled(
        Field<void*>(bombManager, kRenderRegistrationOffset), false);
}

void* CallBombCreate() noexcept {
    using CreateFunction = void* (__cdecl*)();
    return reinterpret_cast<CreateFunction>(
        GameAddresses::kBombManagerCreate)();
}

void CallBombDestroy(void* bombManager) noexcept {
    using DestroyFunction = void (__stdcall*)(void*);
    reinterpret_cast<DestroyFunction>(GameAddresses::kBombManagerDestroy)(
        bombManager);
}

int CallBombStart() noexcept {
    using StartFunction = int (__cdecl*)();
    return reinterpret_cast<StartFunction>(
        GameAddresses::kBombStartDispatch)();
}

int CallBombUpdate(void* bombManager) noexcept {
    int result = 1;
    const uintptr_t function = GameAddresses::kBombUpdateDispatch;
    __asm {
        mov eax, bombManager
        call function
        mov result, eax
    }
    return result;
}

void CallBombConsume() noexcept {
    using ConsumeFunction = void (__cdecl*)();
    reinterpret_cast<ConsumeFunction>(GameAddresses::kBombConsume)();
}

void CallOptionRebuild(void* playerInf) noexcept {
    using OptionRebuildFunction = int (__stdcall*)(void*);
    reinterpret_cast<OptionRebuildFunction>(GameAddresses::kOptionRebuild)(
        playerInf);
}

bool GameplayAllowsBomb() noexcept {
    auto* lifeUi = *reinterpret_cast<uint8_t* volatile*>(
        GameAddresses::kLifeUiManager);
    auto* gameManager = *reinterpret_cast<uint8_t* volatile*>(
        GameAddresses::kGameManager);
    return lifeUi != nullptr && gameManager != nullptr &&
           Field<int32_t>(lifeUi, 0x6D30) == 0 &&
           Field<int32_t>(gameManager, 0x70) != 0;
}

}  // namespace

bool InitializeRuntimeBombs(const std::wstring& root,
                            std::wstring& error) noexcept {
    if (g_configured) {
        return true;
    }
    g_root = root;
    const std::wstring configPath = root + L"\\coop\\config.ini";
    g_enabled = GetPrivateProfileIntW(L"phase10", L"enabled", 1,
                                      configPath.c_str()) != 0;
    g_configured = true;
    if (!g_enabled) {
        Log(L"phase10 P2 Bomb isolation disabled by config");
        return true;
    }
    if (RuntimePlayerContexts() == nullptr) {
        error = L"phase 10 requires phase 3 player contexts";
        g_enabled = false;
        return false;
    }
    Log(L"phase10 P2 Bomb isolation armed; shared inventory retained");
    return true;
}

bool CreateRuntimePlayer2Bomb() noexcept {
    if (!g_enabled) {
        return true;
    }
    if (g_player2Bomb != nullptr) {
        return true;
    }

    PlayerContextManager* contexts = RuntimePlayerContexts();
    DualResourceBanks* banks = RuntimeResourceBanks();
    const PlayerResourceBank* player2Bank =
        banks != nullptr ? banks->Player2() : nullptr;
    if (contexts == nullptr || RuntimePlayer2() == nullptr ||
        contexts->ActiveSlot() != PlayerSlot::kPlayer1 ||
        !contexts->HasContext(PlayerSlot::kPlayer2) ||
        player2Bank == nullptr) {
        Log(L"phase10 P2 Bomb creation skipped: player context or airframe unavailable");
        return false;
    }

    ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
    if (!playerScope.IsActive()) {
        Log(L"phase10 P2 Bomb creation failed: unable to activate P2");
        return false;
    }

    void* bombManager = nullptr;
    {
        ScopedAirframeGlobals airframeScope(player2Bank->airframeIndex);
        ScopedPointerValue bombScope(BombGlobal(), nullptr);
        bombManager = CallBombCreate();
    }
    if (bombManager == nullptr ||
        Field<void*>(bombManager, kUpdateRegistrationOffset) == nullptr ||
        Field<void*>(bombManager, kRenderRegistrationOffset) == nullptr) {
        if (bombManager != nullptr) {
            ScopedAirframeGlobals airframeScope(player2Bank->airframeIndex);
            ScopedPointerValue bombScope(BombGlobal(), bombManager);
            CallBombDestroy(bombManager);
            using FreeFunction = void (__cdecl*)(void*);
            reinterpret_cast<FreeFunction>(GameAddresses::kGameFree)(
                bombManager);
        }
        Log(L"phase10 P2 BombManager allocation or registration failed");
        return false;
    }

    DisableAutomaticCallbacks(bombManager);
    g_player2Bomb = bombManager;
    g_loggedStart = false;
    g_loggedEnd = false;
    Log(L"phase10 independent P2 BombManager created; callbacks=manual");
    return true;
}

void DestroyRuntimePlayer2Bomb() noexcept {
    if (g_player2Bomb == nullptr) {
        return;
    }

    PlayerContextManager* contexts = RuntimePlayerContexts();
    DualResourceBanks* banks = RuntimeResourceBanks();
    const PlayerResourceBank* player2Bank =
        banks != nullptr ? banks->Player2() : nullptr;
    void** anmSlot = PlayerAnmSlot();
    if (contexts == nullptr ||
        !contexts->HasContext(PlayerSlot::kPlayer2) ||
        player2Bank == nullptr || anmSlot == nullptr) {
        DisableAutomaticCallbacks(g_player2Bomb);
        Log(L"phase10 P2 Bomb destroy deferred: context or ANM unavailable");
        return;
    }

    ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
    if (!playerScope.IsActive()) {
        DisableAutomaticCallbacks(g_player2Bomb);
        Log(L"phase10 P2 Bomb destroy deferred: unable to activate P2");
        return;
    }

    void* bombManager = g_player2Bomb;
    DisableAutomaticCallbacks(bombManager);
    {
        ScopedAirframeGlobals airframeScope(player2Bank->airframeIndex);
        ScopedPointerValue slotScope(anmSlot, player2Bank->anmArchive);
        ScopedPointerValue bombScope(BombGlobal(), bombManager);
        CallBombDestroy(bombManager);
    }
    using FreeFunction = void (__cdecl*)(void*);
    reinterpret_cast<FreeFunction>(GameAddresses::kGameFree)(bombManager);
    g_player2Bomb = nullptr;
    Log(L"phase10 independent P2 BombManager destroyed");
}

void UpdateRuntimePlayer2Bomb(uint32_t inputMask, void* playerInf) noexcept {
    if (!g_enabled || g_player2Bomb == nullptr || playerInf == nullptr ||
        playerInf != RuntimePlayer2() || RuntimeDeterminismMovementOnly()) {
        return;
    }

    PlayerContextManager* contexts = RuntimePlayerContexts();
    DualResourceBanks* banks = RuntimeResourceBanks();
    const PlayerResourceBank* player2Bank =
        banks != nullptr ? banks->Player2() : nullptr;
    void** anmSlot = PlayerAnmSlot();
    if (contexts == nullptr || player2Bank == nullptr || anmSlot == nullptr) {
        return;
    }

    ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
    if (!playerScope.IsActive()) {
        return;
    }
    // BombStartDispatch and BombUpdateDispatch select one of six native
    // implementations from the process-wide character/shot globals. Bind
    // those globals for the complete P2 Bomb call window; the player context
    // alone can contain a stale snapshot after nested native callbacks.
    ScopedAirframeGlobals airframeScope(player2Bank->airframeIndex);
    ScopedPointerValue slotScope(anmSlot, player2Bank->anmArchive);
    ScopedPointerValue bombScope(BombGlobal(), g_player2Bomb);

    auto& active = Field<int32_t>(g_player2Bomb, kBombActiveOffset);
    const bool wasActive = active != 0;
    const bool deathbomb = RuntimePlayer2DeathbombAvailable();
    if (!wasActive && (inputMask & kInputBomb) != 0 &&
        (Field<int32_t>(playerInf, kPlayerStateOffset) == 1 || deathbomb) &&
        RuntimeProjectedResource(SharedResource::kBombs) > 0 &&
        GameplayAllowsBomb()) {
        const bool deathbombPrepared =
            deathbomb && PrepareRuntimePlayer2Deathbomb();
        if (CallBombStart() == 0) {
            if (deathbombPrepared) {
                CancelRuntimePlayer2RecoveryForDeathbomb();
            }
            if (RuntimeResourceTransactionsEnabled()) {
                QueueRuntimeResourceDelta(SharedResource::kBombs, -1, 1);
            } else {
                CallBombConsume();
            }
            CallOptionRebuild(playerInf);
            std::wstringstream message;
            message << L"phase10 P2 Bomb started; airframe="
                    << player2Bank->airframeIndex
                    << L"; dispatch_global="
                    << *reinterpret_cast<volatile int32_t*>(
                           GameAddresses::kCharacter)
                    << L"/"
                    << *reinterpret_cast<volatile int32_t*>(
                           GameAddresses::kShotType)
                    << L"; bombs_projected="
                    << RuntimeProjectedResource(SharedResource::kBombs)
                    << L"; vm40="
                    << Field<uint32_t>(g_player2Bomb, kBombVm40Offset)
                    << L"; vm44="
                    << Field<uint32_t>(g_player2Bomb, kBombVm44Offset)
                    << L"; vm48="
                    << Field<uint32_t>(g_player2Bomb, kBombVm48Offset);
            Log(message.str());
            g_loggedStart = true;
            g_loggedEnd = false;
        } else if (deathbombPrepared) {
            RollbackRuntimePlayer2Deathbomb();
        }
    }

    if (active != 0) {
        CallBombUpdate(g_player2Bomb);
    }
    if ((wasActive || g_loggedStart) && active == 0 && !g_loggedEnd) {
        // Sanae B's native completion deletes its secondary VM but leaves the
        // +0x48 ID behind. ANM IDs are recycled, so a later cleanup can delete
        // an unrelated (often Sanae A-looking) VM. Clear all inactive handles
        // only after the native update has performed its own deletion.
        ClearCompletedBombVmHandles(g_player2Bomb);
        Log(L"phase10 P2 Bomb completed without altering P1 Bomb state");
        g_loggedEnd = true;
        g_loggedStart = false;
    }
}

void* RuntimePlayer2BombManager() noexcept {
    return g_player2Bomb;
}

}  // namespace coop::th12
