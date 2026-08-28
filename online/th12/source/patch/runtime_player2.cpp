#include "runtime_player2.h"

#include "coop_rules.h"
#include "log.h"
#include "player2_input.h"
#include "runtime_determinism.h"
#include "runtime_bomb.h"
#include "runtime_frame_input.h"
#include "runtime_player_context.h"
#include "runtime_resources.h"
#include "runtime_resource_tx.h"
#include "version_map.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>

extern "C" int __fastcall Phase8Player2UpdateCallback(void* playerInf) {
    return coop::th12::OnPlayer2UpdateTick(playerInf);
}

extern "C" int __fastcall Phase7Player1UpdateCallback(void* playerInf) {
    return coop::th12::OnPlayer1UpdateTick(playerInf);
}

extern "C" int __fastcall Phase18PlayerRenderCallback(void* playerInf) {
    return coop::th12::OnPlayerRenderTick(playerInf);
}

namespace coop::th12 {
namespace {

constexpr uint32_t kUpdateRegistrationOffset = 0x08;
constexpr uint32_t kRenderRegistrationOffset = 0x0C;
constexpr uint32_t kRegistrationCallbackOffset = 0x08;
constexpr uint32_t kRegistrationFlagsOffset = 0x04;
constexpr uint32_t kRegistrationEnabledFlag = 0x02;
constexpr uint32_t kPositionXOffset = 0x97C;
constexpr uint32_t kPositionYOffset = 0x980;
constexpr uint32_t kFixedPositionXOffset = 0x988;
constexpr uint32_t kFixedPositionYOffset = 0x98C;
constexpr uint32_t kFocusEnableTimerOffset = 0xA48;
constexpr uint32_t kFocusActiveOffset = 0xC598;
constexpr uint32_t kHorizontalVelocityOffset = 0xA14;
constexpr uint32_t kPlayerStateOffset = 0xA28;
constexpr uint32_t kPlayerStatePreviousFrameOffset = 0xA30;
constexpr uint32_t kPlayerStateFrameOffset = 0xA34;
constexpr uint32_t kPlayerStateSubframeOffset = 0xA38;
constexpr uint32_t kPlayerStateScalePointerOffset = 0xA3C;
constexpr uint32_t kPlayerStateTimerFlagsOffset = 0xA40;
constexpr uint32_t kPlayerAnmVmOffset = 0x14;
// Absolute PlayerInf offsets. Relative to the embedded VM these are +0x3C0
// and +0x47C respectively; flag 0x10000 selects the dynamic +0x3C0 color.
constexpr uint32_t kPlayerAnmColorOffset = 0x3D4;
constexpr uint32_t kPlayerAnmFlagsOffset = 0x490;
constexpr uint32_t kFocusHitboxAnmIdOffset = 0x8258;
constexpr uint32_t kPlayerAnmPositionXOffset =
    kPlayerAnmVmOffset + 0x430;
constexpr uint32_t kPlayerAnmPositionYOffset =
    kPlayerAnmVmOffset + 0x434;
constexpr uint32_t kPlayerAnmPositionZOffset =
    kPlayerAnmVmOffset + 0x438;
constexpr uint32_t kCollisionMinimumOffset = 0x9CC;
constexpr uint32_t kCollisionHalfSizeOffset = 0x9E4;
constexpr uint32_t kItemHalfSizeOffset = 0x9F0;
constexpr uint32_t kItemAutoSizeOffset = 0x9FC;
constexpr uint32_t kItemBoundsOffset = 0xC444;
constexpr uint32_t kDamageAreaPoolOffset = 0x8988;
constexpr uint32_t kDamageAreaStride = 0x74;
constexpr uint32_t kDamageAreaCapacity = 0x80;
constexpr uint32_t kInvincibilityPreviousOffset = 0xC400;
constexpr uint32_t kInvincibilityTimerOffset = 0xC404;
constexpr uint32_t kInvincibilitySubframeOffset = 0xC408;
constexpr uint32_t kInvincibilityScalePointerOffset = 0xC40C;
constexpr uint32_t kOptionUpdateFlagsOffset = 0xC414;
constexpr uint32_t kOptionUpdateCountOffset = 0xC418;
constexpr uint32_t kOptionCountOffset = 0xC41C;
constexpr uint32_t kOptionActiveOffset = 0x8260;
constexpr uint32_t kOptionAnmIdOffset = 0x8310;
constexpr uint32_t kOptionStride = 0xE4;
constexpr uint32_t kOptionCapacity = 8;
constexpr int32_t kMaximumPower = 400;
constexpr uint32_t kShotTimerOffset = 0xC420;
constexpr uint32_t kShotSequenceOffset = 0xC424;
constexpr uint32_t kShotSubframeOffset = 0xC428;
constexpr uint32_t kShotTimeScalePointerOffset = 0xC42C;
constexpr uint32_t kShotTimerFlagsOffset = 0xC430;
constexpr uint32_t kGameTimeScaleAddress = 0x004B2ED0;
constexpr int32_t kIdleAnimationScript = 0;
constexpr int32_t kFocusEnableDelay = 4;
// TH12 converts PlayerInf's 17.7 fixed-point positions with the 1/128
// constant at 0x4A3D40. Keep these expressions tied to the decoded gameplay
// coordinates so animation and collision positions cannot silently diverge.
constexpr int32_t kFixedPointScale = 0x80;
constexpr int32_t kMinimumFixedX = -184 * kFixedPointScale;
constexpr int32_t kMaximumFixedX = 184 * kFixedPointScale;
constexpr int32_t kMinimumFixedY = 32 * kFixedPointScale;
constexpr int32_t kMaximumFixedY = 432 * kFixedPointScale;
constexpr int32_t kRespawnStartFixedY = 480 * kFixedPointScale;
constexpr int32_t kRespawnTargetFixedY = 400 * kFixedPointScale;
constexpr uint32_t kDeathDelayFrames = 8;
constexpr int32_t kCoopDeathPowerLoss = 50;
constexpr uint32_t kHitVisualFrames = 8;
constexpr uint32_t kRespawnWaitFrames = 30;
constexpr uint32_t kRespawnEntryFrames = 60;
constexpr uint32_t kRespawnClearEntryFrame = 2;
constexpr int32_t kRespawnInvincibilityFrames = 180;
constexpr float kPlayerRenderOffsetX = 224.0f;
// The original player renderer converts gameplay Y to VM coordinates with +16.
// Keep P2's entry position in gameplay space (480 -> 400) and only apply the
// renderer's coordinate conversion here so the respawn animation starts below
// the playfield instead of appearing in the middle of the stage.
constexpr float kPlayerRenderOffsetY = 16.0f;
constexpr float kDeathItemSpeed = 3.0f;
constexpr float kDeathItemSpreadNumerator = 3.14159274101257f;
constexpr float kDeathItemSpreadDivisor = 28.0f;
constexpr float kDeathItemAngleOffset = 0.392699092626572f;
constexpr float kDeathAimYOffset = 224.0f;
constexpr int32_t kTeammateFadeDistance = 48 * kFixedPointScale;
constexpr uint8_t kTeammateFadeAlpha = 0x80;

static_assert(kShotSequenceOffset + sizeof(int32_t) <=
              GameAddresses::kPlayerInfSize);

std::wstring g_root;
Player2KeyBindings g_keyBindings{
    0x41, 0x44, 0x57, 0x53, VK_SPACE, 0x4A, 0x4B};
int32_t g_startX = 32;
uint32_t g_forcedInputMask = 0;
int32_t g_player1OptionLevel = 0;
int32_t g_player2OptionLevel = 0;
void* g_player2 = nullptr;
void* g_player1UpdateRegistration = nullptr;
void* g_originalPlayer1UpdateCallback = nullptr;
void* g_player1RenderRegistration = nullptr;
void* g_originalPlayer1RenderCallback = nullptr;
bool g_configured = false;
bool g_enabled = false;
bool g_combatEnabled = false;
bool g_loggedMovement = false;
bool g_loggedBoundary = false;
bool g_loggedFocus = false;
bool g_loggedMissingFrame = false;
bool g_loggedShot = false;
bool g_loggedShootInput = false;
bool g_loggedOptionRebuild = false;
bool g_loggedCombatUnavailable = false;
bool g_loggedInvincibilityExpired = false;
bool g_loggedTeammateHitboxDeleted = false;
bool g_player2UpdatedThisTick = false;
// The original game does not guarantee the order of the two PlayerInf update
// registrations.  If P1 is called first, hold that callback until P2 has
// published the merged lockstep frame, then run the P1 update exactly once.
bool g_player1UpdateDeferred = false;
void* g_deferredPlayer1 = nullptr;
uint32_t g_player2AppliedInput = 0;
uint32_t g_player2InvincibilityVisualFrame = 0;
uint32_t g_lastP1OptionRepairFrame = 0;
uint32_t g_lastP2OptionRepairFrame = 0;
bool g_teammateFadeActive = false;
bool g_player1Eliminated = false;
bool g_player1RevivePending = false;
bool g_player1RespawnGranted = false;
bool g_player1FinalDeathPending = false;
uint32_t g_player1FinalDeathFrame = 0;
bool g_player2RevivePending = false;
bool g_player2RespawnGranted = true;
bool g_player2DeathbombActivatedThisTick = false;
bool g_player2DeathbombPrepared = false;

struct PlayerStateTimerSnapshot {
    int32_t previousFrame{};
    int32_t frame{};
    float subframe{};
    uintptr_t scalePointer{};
    uint32_t flags{};
};

PlayerStateTimerSnapshot g_player2PreDeathbombTimer{};

enum class Player2RecoveryPhase : uint8_t {
    kActive,
    kDeathDelay,
    kRespawnWait,
    kEntry,
    kFinalDeath,
    kEliminated,
};

Player2RecoveryPhase g_player2RecoveryPhase = Player2RecoveryPhase::kActive;
uint32_t g_player2RecoveryFrame = 0;

template <typename T>
T& PlayerField(void* playerInf, uint32_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(playerInf) + offset);
}

void Log(const std::wstring& message) noexcept {
    if (!g_root.empty()) {
        coop::WriteLog(g_root, L"patch.log", message);
    }
}

void* GameAllocate(size_t size) noexcept {
    using AllocateFunction = void* (__cdecl*)(size_t);
    return reinterpret_cast<AllocateFunction>(GameAddresses::kGameAllocate)(size);
}

void GameFree(void* allocation) noexcept {
    if (allocation == nullptr) {
        return;
    }
    using FreeFunction = void (__cdecl*)(void*);
    reinterpret_cast<FreeFunction>(GameAddresses::kGameFree)(allocation);
}

void* CallPlayerConstructor(void* playerInf) noexcept {
    using ConstructorFunction = void* (__stdcall*)(void*);
    return reinterpret_cast<ConstructorFunction>(GameAddresses::kPlayerConstructor)(playerInf);
}

int CallPlayerInitialize(void* playerInf) noexcept {
    int result = -1;
    const uintptr_t function = GameAddresses::kPlayerInitialize;
    __asm {
        mov edi, playerInf
        call function
        mov result, eax
    }
    return result;
}

void CallPlayerMovement(void* playerInf) noexcept {
    const uintptr_t function = GameAddresses::kPlayerMovement;
    __asm {
        mov edi, playerInf
        call function
    }
}

void InitializePlayer2CombatState(void* playerInf) noexcept {
    PlayerField<int32_t>(playerInf, kPlayerStateOffset) = 1;
    PlayerField<int32_t>(playerInf, kShotTimerOffset) = -2;
    PlayerField<int32_t>(playerInf, kShotSequenceOffset) = -1;
    PlayerField<float>(playerInf, kShotSubframeOffset) = -1.0f;
    PlayerField<uint32_t>(playerInf, kShotTimeScalePointerOffset) =
        kGameTimeScaleAddress;
    PlayerField<uint32_t>(playerInf, kShotTimerFlagsOffset) |= 1U;
}

void CallOptionRebuild(void* playerInf) noexcept {
    using OptionRebuildFunction = int (__stdcall*)(void*);
    reinterpret_cast<OptionRebuildFunction>(GameAddresses::kOptionRebuild)(playerInf);
}

void CallPlayerShotUpdate(void* playerInf) noexcept {
    using ShotUpdateFunction = int (__stdcall*)(void*);
    reinterpret_cast<ShotUpdateFunction>(GameAddresses::kShotUpdate)(playerInf);
}

void CallPlayerBulletUpdate(void* playerInf) noexcept {
    using BulletUpdateFunction = int (__stdcall*)(void*);
    reinterpret_cast<BulletUpdateFunction>(GameAddresses::kPlayerBulletUpdate)(
        playerInf);
}

class ScopedPlayer2Airframe {
public:
    explicit ScopedPlayer2Airframe(uint32_t airframe) noexcept
        : character_(reinterpret_cast<volatile int32_t*>(GameAddresses::kCharacter)),
          shotType_(reinterpret_cast<volatile int32_t*>(GameAddresses::kShotType)),
          previousCharacter_(*character_), previousShotType_(*shotType_) {
        *character_ = static_cast<int32_t>(airframe / 2);
        *shotType_ = static_cast<int32_t>(airframe % 2);
    }

    ~ScopedPlayer2Airframe() {
        *character_ = previousCharacter_;
        *shotType_ = previousShotType_;
    }

    ScopedPlayer2Airframe(const ScopedPlayer2Airframe&) = delete;
    ScopedPlayer2Airframe& operator=(const ScopedPlayer2Airframe&) = delete;

private:
    volatile int32_t* character_;
    volatile int32_t* shotType_;
    int32_t previousCharacter_;
    int32_t previousShotType_;
};

float CallPlayerAngleToPosition(void* playerInf,
                                const float* position) noexcept {
    float angle = 0.0f;
    const uintptr_t function = GameAddresses::kPlayerAngleToPosition;
    __asm {
        mov eax, position
        mov ecx, playerInf
        call function
        fstp angle
    }
    return angle;
}

bool ApplyPlayer2DeathSettlement(void* playerInf) noexcept {
    const uint8_t rules = RuntimeCoopRules();
    const bool lockPower =
        coop::CoopRuleEnabled(rules, coop::kCoopRuleLockPower);
    const bool lockLives =
        coop::CoopRuleEnabled(rules, coop::kCoopRuleLockLives);
    const bool infiniteRespawn =
        coop::CoopRuleEnabled(rules, coop::kCoopRuleInfiniteRespawn);
    const int32_t loss = lockPower ? 0 : kCoopDeathPowerLoss;
    bool respawnGranted = true;
    if (RuntimeResourceTransactionsEnabled()) {
        const int32_t power =
            RuntimeProjectedResource(SharedResource::kPower);
        const int32_t powerAfterLoss = PowerAfterDeathLoss(power, loss);
        if (powerAfterLoss != power) {
            QueueRuntimeResourceDelta(SharedResource::kPower,
                                      powerAfterLoss - power, 1);
        }
        const int32_t lives =
            RuntimeProjectedResource(SharedResource::kLives);
        respawnGranted = lockLives || infiniteRespawn || lives > 0;
        if (respawnGranted && !lockLives && lives > 0) {
            QueueRuntimeResourceDelta(SharedResource::kLives, -1, 1);
        }
        if (respawnGranted) {
            const int32_t bombs =
                RuntimeProjectedResource(SharedResource::kBombs);
            if (bombs < 2) {
                QueueRuntimeResourceDelta(SharedResource::kBombs,
                                          2 - bombs, 1);
            }
        }
    } else {
        using LosePowerFunction = int (__stdcall*)(int32_t);
        reinterpret_cast<LosePowerFunction>(GameAddresses::kPlayerLosePower)(loss);
    }

    float target[3]{
        0.0f,
        PlayerField<float>(playerInf, kPositionYOffset) - kDeathAimYOffset,
        0.0f,
    };
    const float baseAngle = CallPlayerAngleToPosition(playerInf, target);
    using SpawnItemFunction = void* (__stdcall*)(int32_t, const float*, float,
                                                  float);
    auto spawnItem =
        reinterpret_cast<SpawnItemFunction>(GameAddresses::kItemSpawn);
    const float* position =
        reinterpret_cast<const float*>(static_cast<uint8_t*>(playerInf) +
                                       kPositionXOffset);
    for (int32_t index = 0; index < 7; ++index) {
        const float angle =
            static_cast<float>(index) * kDeathItemSpreadNumerator /
                kDeathItemSpreadDivisor +
            baseAngle - kDeathItemAngleOffset;
        spawnItem(1, position, angle, kDeathItemSpeed);
    }
    g_player2OptionLevel = -1;
    std::wstringstream message;
    message << L"phase11 P2 death settled; Power loss=" << loss
            << L"; floor=100; lock_power=" << (lockPower ? 1 : 0)
            << L"; lock_lives=" << (lockLives ? 1 : 0)
            << L"; infinite_respawn=" << (infiniteRespawn ? 1 : 0)
            << L"; respawn=" << (respawnGranted ? 1 : 0)
            << L"; Bomb floor=" << (respawnGranted ? 2 : 0);
    Log(message.str());
    return respawnGranted;
}

void SpawnPlayerDeathArea(void* playerInf) noexcept {
    const float initialRadius = 32.0f;
    const float secondaryRadius = 16.0f;
    void* position = static_cast<uint8_t*>(playerInf) + kPositionXOffset;
    const uintptr_t function = GameAddresses::kPlayerDeathAreaSpawn;
    __asm {
        push 150
        push 30
        push secondaryRadius
        push initialRadius
        push position
        mov eax, playerInf
        call function
    }
    Log(L"phase9 player death damage area spawned; private per-frame update path");
}

float NormalizeDamageAreaAngle(float angle) noexcept {
    using NormalizeFunction = float (__stdcall*)(float);
    return reinterpret_cast<NormalizeFunction>(
        GameAddresses::kDamageAreaAngleNormalize)(angle);
}

void UpdateDamageAreaMotion(void* motion) noexcept {
    auto& flags = PlayerField<uint32_t>(motion, 0x24);
    if ((flags & 0x02U) == 0) {
        using PolarFunction = void (__thiscall*)(void*, float, float);
        reinterpret_cast<PolarFunction>(GameAddresses::kDamageAreaPolarUpdate)(
            motion, PlayerField<float>(motion, 0x10),
            PlayerField<float>(motion, 0x0C));
        PlayerField<float>(motion, 0x08) = 0.0f;
    } else {
        PlayerField<float>(motion, 0x14) +=
            PlayerField<float>(motion, 0x18);
        const float nextAngle = NormalizeDamageAreaAngle(
            PlayerField<float>(motion, 0x10) +
            PlayerField<float>(motion, 0x0C));
        PlayerField<float>(motion, 0x10) = NormalizeDamageAreaAngle(nextAngle);
    }

    const uintptr_t function = GameAddresses::kDamageAreaVectorUpdate;
    void* position = static_cast<uint8_t*>(motion) - 0x0C;
    __asm {
        push ebx
        mov ebx, position
        call function
        pop ebx
    }
}

void TickPlayer2DamageAreas(void* playerInf) noexcept {
    // This is the inline loop from 0x436F90. PlayerBulletUpdate does not
    // advance this pool; omitting this pass leaves a permanent or inert death
    // area, while running it twice changes damage density and lifetime.
    PlayerField<float>(playerInf, 0x825C) = 1.0f;
    auto* area = static_cast<uint8_t*>(playerInf) + kDamageAreaPoolOffset;
    for (uint32_t index = 0; index < kDamageAreaCapacity;
         ++index, area += kDamageAreaStride) {
        auto& flags = PlayerField<uint32_t>(area, 0x70);
        if ((flags & 1U) == 0) {
            continue;
        }

        auto* motion = static_cast<uint8_t*>(area) + 0x24;
        UpdateDamageAreaMotion(motion);
        PlayerField<float>(area, 0x00) += PlayerField<float>(area, 0x04);
        PlayerField<float>(area, 0x08) += PlayerField<float>(area, 0x0C);
        PlayerField<int32_t>(area, 0x4C) = PlayerField<int32_t>(area, 0x50);

        const uintptr_t scaleAddress = PlayerField<uintptr_t>(area, 0x58);
        float scale = 1.0f;
        if (scaleAddress != 0) {
            scale = *reinterpret_cast<volatile float*>(scaleAddress);
        }
        float remaining = PlayerField<float>(area, 0x54) - scale;
        PlayerField<float>(area, 0x54) = remaining;
        PlayerField<int32_t>(area, 0x50) = static_cast<int32_t>(remaining);
        if (PlayerField<int32_t>(area, 0x50) <= 0) {
            flags &= ~1U;
        }
    }
}

void ClearEnemyBulletsForPlayerDeath() noexcept {
    const uintptr_t bulletFunction =
        GameAddresses::kEnemyBulletScreenClear;
    const uintptr_t laserFunction =
        GameAddresses::kEnemyLaserScreenClear;

    // These two original routines use registers as arguments. This matches the
    // state-0/state-1 respawn calls in 0x436BA0: regular enemy bullets are
    // converted to cancel items first, then laser-like callback objects receive
    // the same screen-clear notification.
    __asm {
        push ebx
        push edi
        mov ebx, 1
        call bulletFunction
        mov edi, 1
        xor ebx, ebx
        call laserFunction
        pop edi
        pop ebx
    }
}

void DestroyAnmVm(uint32_t id, uint32_t reason) noexcept {
    if (id == 0) {
        return;
    }
    const uintptr_t function = GameAddresses::kAnmDeleteVm;
    // Native PlayerUpdate uses 0x461970 for Option teardown. It resolves the
    // VM and its child chain, invokes the ANM callback with BX as the native
    // teardown reason, and updates every child consistently.
    __asm {
        push ebx
        mov ebx, reason
        push id
        call function
        pop ebx
    }
}

void DestroyPlayerOptions(void* playerInf, uint32_t reason = 1) noexcept {
    const int32_t rawCount =
        PlayerField<int32_t>(playerInf, kOptionCountOffset);
    const uint32_t ownedCount = static_cast<uint32_t>((std::clamp)(
        rawCount, 0, static_cast<int32_t>(kOptionCapacity)));
    for (uint32_t index = 0; index < kOptionCapacity; ++index) {
        const uint32_t base = index * kOptionStride;
        const uint32_t active =
            PlayerField<uint32_t>(playerInf, kOptionActiveOffset + base);
        const uint32_t firstId =
            PlayerField<uint32_t>(playerInf, kOptionAnmIdOffset + base);
        const uint32_t secondId =
            PlayerField<uint32_t>(playerInf, kOptionAnmIdOffset + base + 4);

        // ANM IDs are recycled. Native Option transitions can leave an ID in
        // an inactive slot after deleting its VM; deleting that stale ID later
        // can remove an unrelated Bomb/effect that reused it. Only IDs owned
        // by the current count, or by an explicitly active orphan slot, are
        // safe to pass to the native VM destructor.
        if (index < ownedCount || active != 0) {
            DestroyAnmVm(firstId, reason);
            if (secondId != firstId) {
                DestroyAnmVm(secondId, reason);
            }
        }
        PlayerField<uint32_t>(playerInf, kOptionActiveOffset + base) = 0;
        PlayerField<uint32_t>(playerInf,
                              kOptionAnmIdOffset + base) = 0;
        PlayerField<uint32_t>(playerInf,
                              kOptionAnmIdOffset + base + 4) = 0;
    }
    // Native death cleanup clears the scheduler fields alongside the eight VM
    // pairs. P2 bypasses that callback, so clear them at the same boundary.
    PlayerField<uint32_t>(playerInf, kOptionUpdateFlagsOffset) &= ~8U;
    PlayerField<int32_t>(playerInf, kOptionUpdateCountOffset) = 0;
    PlayerField<int32_t>(playerInf, kOptionCountOffset) = 0;
}

bool OptionStateNeedsRepair(void* playerInf, int32_t power) noexcept {
    if (playerInf == nullptr) {
        return false;
    }
    const int32_t optionCount =
        PlayerField<int32_t>(playerInf, kOptionCountOffset);
    if (optionCount < 0 || optionCount > static_cast<int32_t>(kOptionCapacity)) {
        return true;
    }
    // Inactive slots may retain recycled VM IDs, but they must never remain
    // active outside the native count. Such an active tail is the common form
    // of an Option that stays at the bottom of the playfield and keeps firing.
    for (uint32_t index = 0; index < kOptionCapacity; ++index) {
        const uint32_t base = index * kOptionStride;
        const uint32_t active =
            PlayerField<uint32_t>(playerInf, kOptionActiveOffset + base);
        const uint32_t firstVm =
            PlayerField<uint32_t>(playerInf, kOptionAnmIdOffset + base);
        const uint32_t secondVm =
            PlayerField<uint32_t>(playerInf,
                                  kOptionAnmIdOffset + base + 4);
        if (index < static_cast<uint32_t>(optionCount)) {
            // 0x4385B0 creates the secondary VM only at maximum Power and
            // clears it below that threshold. Sanae B uses that second VM for
            // its full-power Option appearance, so losing it must trigger a
            // repair without treating the normal sub-4.00P zero as damage.
            if (active == 0 || firstVm == 0 ||
                (power >= kMaximumPower && secondVm == 0)) {
                return true;
            }
        } else if (active != 0) {
            return true;
        }
    }
    return false;
}

int32_t OptionLevelForPower(int32_t power) noexcept {
    return (std::clamp)(power / 100, 0, 4);
}

void SetPlayerRenderEnabled(void* playerInf, bool enabled) noexcept {
    void* registration =
        PlayerField<void*>(playerInf, kRenderRegistrationOffset);
    if (registration == nullptr) {
        return;
    }
    auto& flags =
        PlayerField<uint32_t>(registration, kRegistrationFlagsOffset);
    if (enabled) {
        flags |= kRegistrationEnabledFlag;
    } else {
        flags &= ~kRegistrationEnabledFlag;
    }
}

void RefreshPlayerCollisionBounds(void* playerInf) noexcept {
    const float x = PlayerField<float>(playerInf, kPositionXOffset);
    const float y = PlayerField<float>(playerInf, kPositionYOffset);
    const float z = PlayerField<float>(playerInf, kPositionYOffset + sizeof(float));

    for (uint32_t axis = 0; axis < 3; ++axis) {
        const float position = axis == 0 ? x : (axis == 1 ? y : z);
        const float collisionHalf =
            PlayerField<float>(playerInf, kCollisionHalfSizeOffset + axis * 4);
        PlayerField<float>(playerInf, kCollisionMinimumOffset + axis * 4) =
            position - collisionHalf;
        PlayerField<float>(playerInf, kCollisionMinimumOffset + 0x0C + axis * 4) =
            position + collisionHalf;

        const float itemHalf =
            PlayerField<float>(playerInf, kItemHalfSizeOffset + axis * 4) * 0.5f;
        PlayerField<float>(playerInf, kItemBoundsOffset + axis * 4) =
            position - itemHalf;
        PlayerField<float>(playerInf, kItemBoundsOffset + 0x0C + axis * 4) =
            position + itemHalf;

        const float autoSize =
            PlayerField<float>(playerInf, kItemAutoSizeOffset + axis * 4);
        PlayerField<float>(playerInf, kItemBoundsOffset + 0x18 + axis * 4) =
            position - autoSize;
        PlayerField<float>(playerInf, kItemBoundsOffset + 0x24 + axis * 4) =
            position + autoSize;

        const float itemSize =
            PlayerField<float>(playerInf, kItemHalfSizeOffset + axis * 4);
        PlayerField<float>(playerInf, kItemBoundsOffset + 0x30 + axis * 4) =
            position - itemSize;
        PlayerField<float>(playerInf, kItemBoundsOffset + 0x3C + axis * 4) =
            position + itemSize;
    }
}

void SetPlayer2StateFrame(void* playerInf, uint32_t frame) noexcept {
    PlayerField<int32_t>(playerInf, kPlayerStatePreviousFrameOffset) =
        frame == 0 ? -1 : static_cast<int32_t>(frame - 1);
    PlayerField<int32_t>(playerInf, kPlayerStateFrameOffset) =
        static_cast<int32_t>(frame);
    PlayerField<float>(playerInf, kPlayerStateSubframeOffset) =
        static_cast<float>(frame);
    PlayerField<uintptr_t>(playerInf, kPlayerStateScalePointerOffset) =
        GameAddresses::kGameTimeScale;
    PlayerField<uint32_t>(playerInf, kPlayerStateTimerFlagsOffset) |= 1U;
}

void AdvancePlayer2StateFrame(void* playerInf) noexcept {
    auto& current = PlayerField<int32_t>(playerInf, kPlayerStateFrameOffset);
    PlayerField<int32_t>(playerInf, kPlayerStatePreviousFrameOffset) = current;
    float scale = 1.0f;
    const uintptr_t scaleAddress = PlayerField<uintptr_t>(
        playerInf, kPlayerStateScalePointerOffset);
    if (scaleAddress == GameAddresses::kGameTimeScale) {
        scale = *reinterpret_cast<volatile float*>(scaleAddress);
    }
    auto& subframe = PlayerField<float>(playerInf, kPlayerStateSubframeOffset);
    subframe += (std::max)(0.0f, scale);
    current = static_cast<int32_t>(subframe);
}

void ApplyPlayer2DeathConfirmationSideEffects() noexcept {
    if (RuntimeResourceTransactionsEnabled()) {
        QueueRuntimeResourceDelta(SharedResource::kPointValue, -0x400, 1);
    } else {
        auto* pointValue = reinterpret_cast<volatile int32_t*>(
            GameAddresses::kPointValue);
        *pointValue = (std::max)(-0x400, *pointValue - 0x400);
    }
    void* manager = *reinterpret_cast<void* volatile*>(GameAddresses::kGameManager);
    if (manager != nullptr) {
        PlayerField<int32_t>(manager, 0x10) += 1;
        PlayerField<int32_t>(manager, 0x18) = 0;
    }
}

void UpdatePlayerInvincibilityVisual(void* playerInf) noexcept {
    const int32_t timer =
        PlayerField<int32_t>(playerInf, kInvincibilityTimerOffset);
    if (timer <= 0) {
        PlayerField<uint32_t>(playerInf, kPlayerAnmFlagsOffset) &= ~0x10000U;
        PlayerField<uint32_t>(playerInf, kPlayerAnmColorOffset) = 0xFFFFFFFFU;
        g_player2InvincibilityVisualFrame = 0;
        return;
    }
    if ((g_player2InvincibilityVisualFrame++ % 3U) == 0) {
        PlayerField<uint32_t>(playerInf, kPlayerAnmFlagsOffset) |= 0x10000U;
        PlayerField<uint32_t>(playerInf, kPlayerAnmColorOffset) = 0xFF0000FFU;
    } else {
        PlayerField<uint32_t>(playerInf, kPlayerAnmFlagsOffset) &= ~0x10000U;
        PlayerField<uint32_t>(playerInf, kPlayerAnmColorOffset) = 0xFFFFFFFFU;
    }
}

void StartPlayerInvincibility(void* playerInf, int32_t frames) noexcept {
    PlayerField<int32_t>(playerInf, kInvincibilityPreviousOffset) = frames;
    PlayerField<int32_t>(playerInf, kInvincibilityTimerOffset) = frames;
    PlayerField<float>(playerInf, kInvincibilitySubframeOffset) =
        static_cast<float>(frames);
    PlayerField<uintptr_t>(playerInf, kInvincibilityScalePointerOffset) =
        GameAddresses::kGameTimeScale;
    g_player2InvincibilityVisualFrame = 0;
    UpdatePlayerInvincibilityVisual(playerInf);
}

void TickPlayerInvincibility(void* playerInf) noexcept {
    auto& timer =
        PlayerField<int32_t>(playerInf, kInvincibilityTimerOffset);
    if (timer <= 0) {
        UpdatePlayerInvincibilityVisual(playerInf);
        return;
    }
    PlayerField<int32_t>(playerInf, kInvincibilityPreviousOffset) = timer;
    auto& remaining =
        PlayerField<float>(playerInf, kInvincibilitySubframeOffset);
    if (remaining <= 0.0f || remaining > static_cast<float>(timer + 1)) {
        remaining = static_cast<float>(timer);
    }
    const uintptr_t scaleAddress = PlayerField<uintptr_t>(
        playerInf, kInvincibilityScalePointerOffset);
    float scale = 1.0f;
    if (scaleAddress == GameAddresses::kGameTimeScale) {
        scale = *reinterpret_cast<volatile float*>(scaleAddress);
    }
    remaining = (std::max)(0.0f, remaining - (std::max)(0.0f, scale));
    timer = static_cast<int32_t>(remaining);
    UpdatePlayerInvincibilityVisual(playerInf);
    if (timer == 0 && !g_loggedInvincibilityExpired) {
        Log(L"phase9 P2 initial invincibility expired; lethal collision armed");
        g_loggedInvincibilityExpired = true;
    }
}

void RestoreIdleAnimation(void* playerInf) noexcept;

void SyncPlayerAnimationPosition(void* playerInf) noexcept {
    PlayerField<float>(playerInf, kPlayerAnmPositionXOffset) =
        PlayerField<float>(playerInf, kPositionXOffset) + kPlayerRenderOffsetX;
    PlayerField<float>(playerInf, kPlayerAnmPositionYOffset) =
        PlayerField<float>(playerInf, kPositionYOffset) + kPlayerRenderOffsetY;
    PlayerField<float>(playerInf, kPlayerAnmPositionZOffset) =
        PlayerField<float>(playerInf, kPositionYOffset + sizeof(float));
}

void SetPlayerPosition(void* playerInf, int32_t fixedX,
                       int32_t fixedY) noexcept {
    PlayerField<int32_t>(playerInf, kFixedPositionXOffset) = fixedX;
    PlayerField<int32_t>(playerInf, kFixedPositionYOffset) = fixedY;
    PlayerField<float>(playerInf, kPositionXOffset) =
        static_cast<float>(fixedX) / static_cast<float>(kFixedPointScale);
    PlayerField<float>(playerInf, kPositionYOffset) =
        static_cast<float>(fixedY) / static_cast<float>(kFixedPointScale);
    SyncPlayerAnimationPosition(playerInf);
}

void StartPlayer2Entry(void* playerInf, bool createDeathClear) noexcept {
    if (createDeathClear) {
        SpawnPlayerDeathArea(playerInf);
    }
    SetPlayerPosition(playerInf, 0, kRespawnStartFixedY);
    PlayerField<int32_t>(playerInf, kPlayerStateOffset) = 0;
    SetPlayer2StateFrame(playerInf, 0);
    g_player2RecoveryPhase = Player2RecoveryPhase::kEntry;
    g_player2RecoveryFrame = 0;
    RestoreIdleAnimation(playerInf);
    SyncPlayerAnimationPosition(playerInf);
    StartPlayerInvincibility(
        playerInf,
        kRespawnInvincibilityFrames +
            static_cast<int32_t>(kRespawnEntryFrames));
    SetPlayerRenderEnabled(playerInf, true);
    RefreshPlayerCollisionBounds(playerInf);
}

bool UpdatePlayer2Recovery(void* playerInf) noexcept {
    auto& state = PlayerField<int32_t>(playerInf, kPlayerStateOffset);
    if (g_player2RecoveryPhase == Player2RecoveryPhase::kActive) {
        if (state == 1) {
            return false;
        }
        g_player2RecoveryPhase = Player2RecoveryPhase::kDeathDelay;
        g_player2RecoveryFrame = 0;
        g_player2RespawnGranted = true;
        PlayerField<int32_t>(playerInf, kHorizontalVelocityOffset) = 0;
        PlayerField<int32_t>(playerInf, kFocusActiveOffset) = 0;
        // The original collision routine has already installed the hit ANM.
        // Keep the player visible for its eight-frame impact animation, then
        // hide it at the same boundary where the original game removes Option.
        SetPlayerRenderEnabled(playerInf, true);
        // Remove Option VMs as soon as the hit is accepted.  The original
        // player keeps these around until its death settlement, but P2 does
        // not enter that update path; delaying this cleanup leaves orphaned
        // sprites when the bullet/ANM scheduler advances independently.
        DestroyPlayerOptions(playerInf);
        Log(L"phase9 P2 private recovery started; shared resources untouched");
    }

    switch (g_player2RecoveryPhase) {
    case Player2RecoveryPhase::kDeathDelay:
        state = 4;
        ++g_player2RecoveryFrame;
        SetPlayer2StateFrame(playerInf, g_player2RecoveryFrame);
        if (g_player2RecoveryFrame >= kHitVisualFrames) {
            SetPlayerRenderEnabled(playerInf, false);
            DestroyPlayerOptions(playerInf);
        }
        if (g_player2RecoveryFrame >= kDeathDelayFrames) {
            state = 2;
            ApplyPlayer2DeathConfirmationSideEffects();
            g_player2RecoveryPhase = Player2RecoveryPhase::kRespawnWait;
            g_player2RecoveryFrame = 0;
            SetPlayer2StateFrame(playerInf, 0);
            Log(L"phase9 P2 death confirmed; recovery wait started");
        }
        return true;

    case Player2RecoveryPhase::kRespawnWait:
        state = 2;
        ++g_player2RecoveryFrame;
        SetPlayer2StateFrame(playerInf, g_player2RecoveryFrame);
        if (g_player2RecoveryFrame == 3) {
            g_player2RespawnGranted =
                ApplyPlayer2DeathSettlement(playerInf);
        }
        if (g_player2RecoveryFrame >= kRespawnWaitFrames) {
            if (!g_player2RespawnGranted &&
                RuntimeResourceTransactionsEnabled() &&
                RuntimeProjectedResource(SharedResource::kLives) > 0) {
                QueueRuntimeResourceDelta(SharedResource::kLives, -1, 1);
                const int32_t bombs =
                    RuntimeProjectedResource(SharedResource::kBombs);
                if (bombs < 2) {
                    QueueRuntimeResourceDelta(SharedResource::kBombs,
                                              2 - bombs, 1);
                }
                g_player2RespawnGranted = true;
                Log(L"phase11 late shared life reserved for P2 respawn");
            }
            if (g_player2RespawnGranted) {
                StartPlayer2Entry(playerInf, true);
                Log(L"phase9 P2 private recovery entry started");
            } else {
                SpawnPlayerDeathArea(playerInf);
                state = 0;
                SetPlayer2StateFrame(playerInf, 0);
                g_player2RecoveryPhase = Player2RecoveryPhase::kFinalDeath;
                g_player2RecoveryFrame = 0;
                SetPlayerRenderEnabled(playerInf, false);
                DestroyPlayerOptions(playerInf);
                Log(L"phase11 P2 final death effects started before elimination");
            }
        }
        return true;

    case Player2RecoveryPhase::kEntry: {
        state = 0;
        const int32_t x =
            PlayerField<int32_t>(playerInf, kFixedPositionXOffset);
        const uint32_t entryFrame =
            (std::min)(++g_player2RecoveryFrame, kRespawnEntryFrames);
        SetPlayer2StateFrame(playerInf, entryFrame);
        const int32_t travel = kRespawnStartFixedY - kRespawnTargetFixedY;
        const int32_t nextY = kRespawnStartFixedY -
            static_cast<int32_t>(entryFrame * travel / kRespawnEntryFrames);
        SetPlayerPosition(playerInf, x, nextY);
        RefreshPlayerCollisionBounds(playerInf);
        if (entryFrame == kRespawnClearEntryFrame) {
            ClearEnemyBulletsForPlayerDeath();
            Log(L"phase9 P2 enemy bullet/laser clear applied at recovery frame 40");
        }
        if (entryFrame < kRespawnEntryFrames) {
            TickPlayerInvincibility(playerInf);
            return true;
        }

        state = 1;
        SetPlayer2StateFrame(playerInf, 0);
        g_player2OptionLevel = -1;
        g_player2RecoveryPhase = Player2RecoveryPhase::kActive;
        g_player2RecoveryFrame = 0;
        RestoreIdleAnimation(playerInf);
        SyncPlayerAnimationPosition(playerInf);
        SetPlayerRenderEnabled(playerInf, true);
        Log(L"phase9 P2 private recovery completed; invincibility=180");
        return true;
    }

    case Player2RecoveryPhase::kActive:
        return false;

    case Player2RecoveryPhase::kFinalDeath:
        state = 0;
        SetPlayerRenderEnabled(playerInf, false);
        DestroyPlayerOptions(playerInf);
        ++g_player2RecoveryFrame;
        SetPlayer2StateFrame(playerInf, g_player2RecoveryFrame);
        if (g_player2RecoveryFrame < kRespawnClearEntryFrame) {
            return true;
        }
        ClearEnemyBulletsForPlayerDeath();
        Log(L"phase11 P2 final death damage/clear completed at recovery frame 40");
        if (g_player2RevivePending) {
            g_player2RevivePending = false;
            if (RuntimeResourceTransactionsEnabled()) {
                const int32_t bombs =
                    RuntimeProjectedResource(SharedResource::kBombs);
                if (bombs < 2) {
                    QueueRuntimeResourceDelta(SharedResource::kBombs,
                                              2 - bombs, 1);
                }
            }
            StartPlayer2Entry(playerInf, false);
            Log(L"phase11 P2 final death followed by queued shared 1UP revival");
            return true;
        }
        state = 2;
        SetPlayer2StateFrame(playerInf, 0);
        g_player2RecoveryPhase = Player2RecoveryPhase::kEliminated;
        g_player2RecoveryFrame = 0;
        return true;

    case Player2RecoveryPhase::kEliminated:
        state = 2;
        SetPlayer2StateFrame(playerInf, g_player2RecoveryFrame);
        SetPlayerRenderEnabled(playerInf, false);
        // Game-over can advance on the same scheduler cycle as the second
        // death. Reassert the cleanup so no late Option VM survives that edge.
        DestroyPlayerOptions(playerInf);
        if (!g_player2RevivePending) {
            return true;
        }
        g_player2RevivePending = false;
        if (RuntimeResourceTransactionsEnabled()) {
            const int32_t bombs =
                RuntimeProjectedResource(SharedResource::kBombs);
            if (bombs < 2) {
                QueueRuntimeResourceDelta(SharedResource::kBombs,
                                          2 - bombs, 1);
            }
        }
        StartPlayer2Entry(playerInf, false);
        Log(L"phase11 P2 revived by shared 1UP event");
        return true;
    }
    return true;
}

void RestoreIdleAnimation(void* playerInf) noexcept {
    using SetAnimationScript = void (__thiscall*)(void*, void*, int32_t);
    void* archive = PlayerField<void*>(playerInf,
                                      GameAddresses::kPlayerAnmPointerOffset);
    void* animationVm = static_cast<uint8_t*>(playerInf) + kPlayerAnmVmOffset;
    reinterpret_cast<SetAnimationScript>(GameAddresses::kAnmSetScript)(
        archive, animationVm, kIdleAnimationScript);
}

void CallPlayerDestroy(void* playerInf) noexcept {
    using DestroyFunction = void (__stdcall*)(void*);
    reinterpret_cast<DestroyFunction>(GameAddresses::kPlayerDestroy)(playerInf);
}

void** PlayerAnmSlot() noexcept {
    const auto manager = *reinterpret_cast<uint8_t**>(GameAddresses::kAnmManagerRoot);
    if (manager == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void**>(manager + GameAddresses::kAnmSlotTableOffset +
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

bool RebuildPlayerOptions(void* playerInf,
                          const PlayerResourceBank* bank,
                          bool forceRepair = false) noexcept {
    if (playerInf == nullptr || bank == nullptr || !bank->IsReady()) {
        return false;
    }
    void** anmSlot = PlayerAnmSlot();
    if (anmSlot == nullptr) {
        return false;
    }
    ScopedPlayer2Airframe airframeScope(bank->airframeIndex);
    ScopedPointerValue slotScope(anmSlot, bank->anmArchive);
    // 0x4385B0 is the original six-airframe Option lifecycle routine. It
    // retires the old primary/secondary VMs itself before installing the new
    // scripts. Pre-deleting a healthy set here makes the same transition run
    // twice and can invalidate an ANM handle that the native power-up/Bomb
    // path still owns for the remainder of the frame.
    //
    // Keep the hard reset only for a structurally damaged PlayerInf. Death
    // and object teardown continue to call DestroyPlayerOptions directly.
    if (forceRepair) {
        DestroyPlayerOptions(playerInf);
    }
    CallOptionRebuild(playerInf);
    return true;
}

void SetTeammateHitboxHidden(void* teammate, bool hidden) noexcept {
    const uint32_t hitboxVmId = teammate != nullptr
        ? PlayerField<uint32_t>(teammate, kFocusHitboxAnmIdOffset)
        : 0;
    if (!hidden || hitboxVmId == 0) {
        return;
    }
    // The original focus update recreates this VM whenever focus is active and
    // the field is zero. Delete the teammate VM immediately after its update,
    // using the same reason=1 path as PlayerUpdate. Merely clearing render flag
    // 0x02 is insufficient because the ANM script can enable it again later in
    // the same frame.
    DestroyAnmVm(hitboxVmId, 1);
    PlayerField<uint32_t>(teammate, kFocusHitboxAnmIdOffset) = 0;
    if (!g_loggedTeammateHitboxDeleted) {
        std::wstringstream message;
        message << L"phase8 teammate focus hitbox VM deleted; id="
                << hitboxVmId;
        Log(message.str());
        g_loggedTeammateHitboxDeleted = true;
    }
}

void ApplyTeammateFade(void* player1, void* player2) noexcept {
    if (player1 == nullptr || player2 == nullptr) {
        return;
    }
    const bool localIsPlayer2 =
        RuntimeLocalPlayerSlot() == LogicalPlayerSlot::kPlayer2;
    // Never draw the teammate's focus hitbox in either local or network play.
    // Collision and focus state are untouched; only the teammate's ANM VM is
    // hidden, so the local player's own hitbox remains visible.
    SetTeammateHitboxHidden(localIsPlayer2 ? player1 : player2, true);

    // Rendering is process-local, so only the remote player's sprite is
    // faded.  This keeps the controlled ship fully opaque on each machine;
    // changing the render color is deliberately excluded from the state hash.
    if (!RuntimeNetworkActive()) {
        g_teammateFadeActive = false;
        return;
    }
    const bool bothActive =
        PlayerField<int32_t>(player1, kPlayerStateOffset) == 1 &&
        PlayerField<int32_t>(player2, kPlayerStateOffset) == 1;
    bool close = false;
    if (bothActive) {
        const int64_t dx = static_cast<int64_t>(
            PlayerField<int32_t>(player1, kFixedPositionXOffset)) -
            PlayerField<int32_t>(player2, kFixedPositionXOffset);
        const int64_t dy = static_cast<int64_t>(
            PlayerField<int32_t>(player1, kFixedPositionYOffset)) -
            PlayerField<int32_t>(player2, kFixedPositionYOffset);
        const int64_t distanceSquared = dx * dx + dy * dy;
        const int64_t threshold = kTeammateFadeDistance;
        close = distanceSquared <= threshold * threshold;
    }
    if (close != g_teammateFadeActive) {
        g_teammateFadeActive = close;
        Log(close ? L"phase8 teammate fade enabled; distance<=48px"
                  : L"phase8 teammate fade disabled; players separated");
    }
}

bool IsPressed(uint32_t virtualKey) noexcept {
    return (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
}

uint32_t ReadPlayer2Input() noexcept {
    if (g_forcedInputMask != 0) {
        return g_forcedInputMask;
    }
    return BuildPlayer2InputMask({
        IsPressed(g_keyBindings.left),
        IsPressed(g_keyBindings.right),
        IsPressed(g_keyBindings.up),
        IsPressed(g_keyBindings.down),
        IsPressed(g_keyBindings.focus),
        g_combatEnabled && IsPressed(g_keyBindings.shoot),
        g_combatEnabled && IsPressed(g_keyBindings.bomb),
    });
}

uint32_t ReadVirtualKey(const wchar_t* configPath, const wchar_t* section,
                        const wchar_t* name, uint32_t fallback) noexcept {
    return static_cast<uint32_t>(GetPrivateProfileIntW(
        section, name, static_cast<int>(fallback), configPath));
}

void ClearFailedPlayer2(PlayerContextManager& contexts, void* playerInf) noexcept {
    *reinterpret_cast<void* volatile*>(GameAddresses::kPlayerSingleton) =
        contexts.GetContext(PlayerSlot::kPlayer1)->playerInf;
    contexts.ClearInactive(PlayerSlot::kPlayer2);
    GameFree(playerInf);
}

void RestorePlayer1UpdateCallback() noexcept {
    if (g_player1UpdateRegistration != nullptr &&
        g_originalPlayer1UpdateCallback != nullptr) {
        PlayerField<void*>(g_player1UpdateRegistration,
                           kRegistrationCallbackOffset) =
            g_originalPlayer1UpdateCallback;
    }
    g_player1UpdateRegistration = nullptr;
    g_originalPlayer1UpdateCallback = nullptr;
}

void RestorePlayer1RenderCallback() noexcept {
    if (g_player1RenderRegistration != nullptr &&
        g_originalPlayer1RenderCallback != nullptr &&
        PlayerField<void*>(g_player1RenderRegistration,
                           kRegistrationCallbackOffset) ==
            reinterpret_cast<void*>(&Phase18PlayerRenderCallback)) {
        PlayerField<void*>(g_player1RenderRegistration,
                           kRegistrationCallbackOffset) =
            g_originalPlayer1RenderCallback;
    }
    g_player1RenderRegistration = nullptr;
    g_originalPlayer1RenderCallback = nullptr;
}

}  // namespace

bool InitializeRuntimePlayer2(const std::wstring& root, std::wstring& error) noexcept {
    if (g_configured) {
        return true;
    }
    g_root = root;
    const std::wstring configPath = root + L"\\coop\\config.ini";
    if (GetPrivateProfileIntW(L"phase5", L"enabled", 1, configPath.c_str()) == 0) {
        g_configured = true;
        Log(L"phase5 P2 movement disabled by config");
        return true;
    }
    if (RuntimeResourceBanks() == nullptr || RuntimePlayerContexts() == nullptr) {
        error = L"phase 5 requires phase 3 contexts and phase 4 resource hooks";
        return false;
    }

    g_keyBindings = {
        ReadVirtualKey(configPath.c_str(), L"phase5", L"p2_left", 0x41),
        ReadVirtualKey(configPath.c_str(), L"phase5", L"p2_right", 0x44),
        ReadVirtualKey(configPath.c_str(), L"phase5", L"p2_up", 0x57),
        ReadVirtualKey(configPath.c_str(), L"phase5", L"p2_down", 0x53),
        ReadVirtualKey(configPath.c_str(), L"phase5", L"p2_focus", VK_SPACE),
        ReadVirtualKey(configPath.c_str(), L"phase8", L"p2_shoot", 0x4A),
        ReadVirtualKey(configPath.c_str(), L"phase8", L"p2_bomb", 0x4B),
    };
    if (!g_keyBindings.IsValid()) {
        error = L"P2 movement, focus, shoot and bomb keys must be distinct values from 1 through 254";
        return false;
    }
    g_combatEnabled =
        GetPrivateProfileIntW(L"phase8", L"enabled", 1, configPath.c_str()) != 0;
    g_startX = GetPrivateProfileIntW(L"phase5", L"p2_start_x", 32,
                                     configPath.c_str());
    if (g_startX < -184 || g_startX > 184) {
        error = L"phase5.p2_start_x must be inside the playfield (-184 through 184)";
        return false;
    }
    g_forcedInputMask = static_cast<uint32_t>(GetPrivateProfileIntW(
        L"phase5", L"diagnostic_forced_input", 0, configPath.c_str()));
    const uint32_t supportedInput =
        kInputMovementMask | kInputFocus |
        (g_combatEnabled ? (kInputShoot | kInputBomb) : 0U);
    if ((g_forcedInputMask & ~supportedInput) != 0) {
        error = L"phase5.diagnostic_forced_input contains unsupported input bits";
        return false;
    }

    g_enabled = true;
    g_configured = true;
    return true;
}

bool CreateRuntimePlayer2() noexcept {
    if (!g_enabled) {
        return true;
    }
    if (g_player2 != nullptr) {
        return true;
    }

    DualResourceBanks* banks = RuntimeResourceBanks();
    PlayerContextManager* contexts = RuntimePlayerContexts();
    const PlayerResourceBank* player1Bank = banks != nullptr ? banks->Player1() : nullptr;
    const PlayerResourceBank* player2Bank = banks != nullptr ? banks->Player2() : nullptr;
    if (contexts == nullptr || contexts->ActiveSlot() != PlayerSlot::kPlayer1 ||
        player1Bank == nullptr || player2Bank == nullptr || !banks->IsReady()) {
        Log(L"phase5 P2 creation skipped: contexts or resource banks are not ready");
        return false;
    }
    if (!contexts->SnapshotActive()) {
        Log(L"phase5 P2 creation skipped: unable to capture current P1 context");
        return false;
    }
    const PlayerContext* player1Context = contexts->GetContext(PlayerSlot::kPlayer1);
    void** anmSlot = PlayerAnmSlot();
    auto* preloadedSht = reinterpret_cast<void**>(GameAddresses::kPreloadedPlayerSht);
    if (player1Context == nullptr || player1Context->playerInf == nullptr ||
        anmSlot == nullptr || *anmSlot != player1Bank->anmArchive ||
        *preloadedSht != nullptr) {
        Log(L"phase5 P2 creation skipped: original player resource state is unexpected");
        return false;
    }

    void* player2 = GameAllocate(GameAddresses::kPlayerInfSize);
    if (player2 == nullptr || CallPlayerConstructor(player2) != player2) {
        GameFree(player2);
        *reinterpret_cast<void* volatile*>(GameAddresses::kPlayerSingleton) =
            player1Context->playerInf;
        Log(L"phase5 P2 creation failed: PlayerInf allocation or construction failed");
        return false;
    }
    *reinterpret_cast<void* volatile*>(GameAddresses::kPlayerSingleton) =
        player1Context->playerInf;

    const uint32_t airframe = player2Bank->airframeIndex;
    const PlayerContext player2Context{
        static_cast<int32_t>(airframe / 2),
        static_cast<int32_t>(airframe % 2),
        0,
        player2,
    };
    if (!contexts->ConfigureInactive(PlayerSlot::kPlayer2, player2Context)) {
        ClearFailedPlayer2(*contexts, player2);
        Log(L"phase5 P2 creation failed: unable to configure P2 context");
        return false;
    }

    int initializeResult = -1;
    {
        ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
        if (!playerScope.IsActive()) {
            ClearFailedPlayer2(*contexts, player2);
            Log(L"phase5 P2 creation failed: unable to activate P2 context");
            return false;
        }
        ScopedPointerValue slotScope(anmSlot, player2Bank->anmArchive);
        ScopedPointerValue shtScope(preloadedSht, player2Bank->shtData);
        initializeResult = CallPlayerInitialize(player2);
    }
    if (initializeResult != 0) {
        ClearFailedPlayer2(*contexts, player2);
        Log(L"phase5 P2 creation failed: original PlayerInf initialization failed");
        return false;
    }
    if (!BindPlayer2Resources(player2) ||
        PlayerField<void*>(player2, kUpdateRegistrationOffset) == nullptr ||
        PlayerField<void*>(player2, kRenderRegistrationOffset) == nullptr) {
        g_player2 = player2;
        DestroyRuntimePlayer2();
        Log(L"phase5 P2 creation failed: initialized PlayerInf validation failed");
        return false;
    }

    InitializePlayer2CombatState(player2);
    SetPlayer2StateFrame(player2, 0);
    if (PlayerField<int32_t>(player2, kPlayerStateOffset) != 1) {
        g_player2 = player2;
        DestroyRuntimePlayer2();
        Log(L"phase8 P2 activation failed: player state did not become active");
        return false;
    }
    const int32_t initialOptionLevel = OptionLevelForPower(
        *reinterpret_cast<volatile int32_t*>(GameAddresses::kPower));
    g_player1OptionLevel = initialOptionLevel;
    g_player2OptionLevel = initialOptionLevel;

    void* updateRegistration = PlayerField<void*>(player2, kUpdateRegistrationOffset);
    void* renderRegistration = PlayerField<void*>(player2, kRenderRegistrationOffset);
    PlayerField<void*>(updateRegistration, kRegistrationCallbackOffset) =
        reinterpret_cast<void*>(&Phase8Player2UpdateCallback);
    PlayerField<uint32_t>(updateRegistration, kRegistrationFlagsOffset) |=
        kRegistrationEnabledFlag;
    PlayerField<uint32_t>(renderRegistration, kRegistrationFlagsOffset) |=
        kRegistrationEnabledFlag;
    PlayerField<void*>(renderRegistration, kRegistrationCallbackOffset) =
        reinterpret_cast<void*>(&Phase18PlayerRenderCallback);

    const int32_t fixedX = g_startX * kFixedPointScale;
    PlayerField<int32_t>(player2, kFixedPositionXOffset) = fixedX;
    PlayerField<float>(player2, kPositionXOffset) = static_cast<float>(g_startX);
    PlayerField<float>(player2, kPositionYOffset) =
        static_cast<float>(PlayerField<int32_t>(player2, kFixedPositionYOffset)) /
        static_cast<float>(kFixedPointScale);
    g_player2 = player2;
    void* player1UpdateRegistration =
        PlayerField<void*>(player1Context->playerInf, kUpdateRegistrationOffset);
    void* player1RenderRegistration =
        PlayerField<void*>(player1Context->playerInf, kRenderRegistrationOffset);
    if (player1UpdateRegistration == nullptr ||
        player1RenderRegistration == nullptr ||
        PlayerField<void*>(player1UpdateRegistration,
                           kRegistrationCallbackOffset) !=
            reinterpret_cast<void*>(GameAddresses::kPlayerUpdateCallback) ||
        PlayerField<void*>(player1RenderRegistration,
                           kRegistrationCallbackOffset) !=
            reinterpret_cast<void*>(GameAddresses::kPlayerRenderCallback)) {
        DestroyRuntimePlayer2();
        Log(L"phase7 P1 frame anchor unavailable: unexpected update callback");
        return false;
    }
    g_player1UpdateRegistration = player1UpdateRegistration;
    g_originalPlayer1UpdateCallback =
        PlayerField<void*>(player1UpdateRegistration,
                           kRegistrationCallbackOffset);
    PlayerField<void*>(player1UpdateRegistration, kRegistrationCallbackOffset) =
        reinterpret_cast<void*>(&Phase7Player1UpdateCallback);
    g_player1RenderRegistration = player1RenderRegistration;
    g_originalPlayer1RenderCallback =
        PlayerField<void*>(player1RenderRegistration,
                           kRegistrationCallbackOffset);
    PlayerField<void*>(player1RenderRegistration, kRegistrationCallbackOffset) =
        reinterpret_cast<void*>(&Phase18PlayerRenderCallback);
    ResetRuntimeFrameInputTimeline();
    ResetRuntimeDeterminismTrace();
    g_loggedMovement = false;
    g_loggedBoundary = false;
    g_loggedFocus = false;
    g_loggedMissingFrame = false;
    g_loggedShot = false;
    g_loggedShootInput = false;
    g_loggedOptionRebuild = false;
    g_loggedCombatUnavailable = false;
    g_loggedInvincibilityExpired = false;
    g_loggedTeammateHitboxDeleted = false;
    g_player2UpdatedThisTick = false;
    g_player1UpdateDeferred = false;
    g_deferredPlayer1 = nullptr;
    g_player2AppliedInput = 0;
    g_player1OptionLevel = initialOptionLevel;
    g_player2OptionLevel = initialOptionLevel;
    g_lastP1OptionRepairFrame = 0;
    g_lastP2OptionRepairFrame = 0;
    g_player2InvincibilityVisualFrame = 0;
    g_player2RecoveryPhase = Player2RecoveryPhase::kActive;
    g_player2RecoveryFrame = 0;
    g_player1Eliminated = false;
    g_player1RevivePending = false;
    g_player1RespawnGranted = false;
    g_player1FinalDeathPending = false;
    g_player1FinalDeathFrame = 0;
    g_player2RevivePending = false;
    g_player2RespawnGranted = true;
    g_player2DeathbombActivatedThisTick = false;
    g_player2DeathbombPrepared = false;
    g_player2PreDeathbombTimer = {};
    g_teammateFadeActive = false;

    if (!CreateRuntimePlayer2Bomb()) {
        DestroyRuntimePlayer2();
        Log(L"phase10 P2 BombManager creation failed; P2 disabled");
        return false;
    }

    std::wstringstream message;
    message << L"phase5 P2 created; airframe=" << airframe
            << L"; controls=WASD+Space/J; position=(" << g_startX << L","
            << PlayerField<float>(player2, kPositionYOffset) << L")"
            << L"; update="
            << (g_combatEnabled ? L"movement+shot+option" : L"movement-only")
            << L"; combat_storage=isolated; render=original"
            << L"; callback_order=p2_then_p1"
            << L"; sample_anchor=global_update_chain_exit";
    Log(message.str());
    return true;
}

void DestroyRuntimePlayer2() noexcept {
    if (g_player2 == nullptr) {
        return;
    }
    PlayerContextManager* contexts = RuntimePlayerContexts();
    if (contexts == nullptr || contexts->ActiveSlot() != PlayerSlot::kPlayer1 ||
        !contexts->HasContext(PlayerSlot::kPlayer2)) {
        Log(L"phase5 P2 destroy deferred: player contexts are unavailable");
        return;
    }

    void* player2 = g_player2;
    g_teammateFadeActive = false;
    FinishRuntimeDeterminismTrace();
    RestorePlayer1UpdateCallback();
    RestorePlayer1RenderCallback();
    DestroyRuntimePlayer2Bomb();
    CaptureRuntimeNativeResourceChanges(0);
    CommitRuntimeResourceFrame();
    {
        ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
        if (!playerScope.IsActive()) {
            Log(L"phase5 P2 destroy deferred: unable to activate P2 context");
            return;
        }
        auto* preserveFlags =
            reinterpret_cast<volatile uint32_t*>(GameAddresses::kPlayerPreserveResourcesFlags);
        auto* preloadedSht = reinterpret_cast<void**>(GameAddresses::kPreloadedPlayerSht);
        const uint32_t originalFlags = *preserveFlags;
        void* originalPreloadedSht = *preloadedSht;
        *preserveFlags = originalFlags | 1U;
        // The native PlayerDestroy path normally owns this teardown. P2 has
        // a private update path, so perform the final reason=2 VM cleanup
        // explicitly before handing the object back to the original destroy.
        DestroyPlayerOptions(player2, 2);
        CallPlayerDestroy(player2);
        *preloadedSht = originalPreloadedSht;
        *preserveFlags = originalFlags;
    }

    GameFree(player2);
    g_player2 = nullptr;
    g_player2UpdatedThisTick = false;
    g_player1UpdateDeferred = false;
    g_deferredPlayer1 = nullptr;
    g_player2AppliedInput = 0;
    g_player1OptionLevel = 0;
    g_player2OptionLevel = 0;
    g_lastP1OptionRepairFrame = 0;
    g_lastP2OptionRepairFrame = 0;
    g_player2InvincibilityVisualFrame = 0;
    g_player2RecoveryPhase = Player2RecoveryPhase::kActive;
    g_player2RecoveryFrame = 0;
    g_player1Eliminated = false;
    g_player1RevivePending = false;
    g_player1RespawnGranted = false;
    g_player1FinalDeathPending = false;
    g_player1FinalDeathFrame = 0;
    g_player2RevivePending = false;
    g_player2RespawnGranted = true;
    g_player2DeathbombActivatedThisTick = false;
    g_player2DeathbombPrepared = false;
    g_player2PreDeathbombTimer = {};
    contexts->ClearInactive(PlayerSlot::kPlayer2);
    g_loggedTeammateHitboxDeleted = false;
    Log(L"phase5 P2 destroyed before resource-bank release");
}

void* RuntimePlayer2() noexcept {
    return g_player2;
}

bool RuntimePlayer1Eliminated() noexcept {
    return g_player1Eliminated;
}

uint32_t RuntimePlayer2RecoveryState() noexcept {
    return static_cast<uint32_t>(g_player2RecoveryPhase) |
           (g_player2RecoveryFrame << 8U);
}

bool RuntimePlayer2DeathbombAvailable() noexcept {
    return g_player2 != nullptr &&
           PlayerField<int32_t>(g_player2, kPlayerStateOffset) == 4 &&
           (g_player2RecoveryPhase == Player2RecoveryPhase::kActive ||
            (g_player2RecoveryPhase == Player2RecoveryPhase::kDeathDelay &&
             g_player2RecoveryFrame < kDeathDelayFrames));
}

bool PrepareRuntimePlayer2Deathbomb() noexcept {
    if (!RuntimePlayer2DeathbombAvailable() || g_player2DeathbombPrepared) {
        return false;
    }
    g_player2PreDeathbombTimer = {
        PlayerField<int32_t>(g_player2, kPlayerStatePreviousFrameOffset),
        PlayerField<int32_t>(g_player2, kPlayerStateFrameOffset),
        PlayerField<float>(g_player2, kPlayerStateSubframeOffset),
        PlayerField<uintptr_t>(g_player2, kPlayerStateScalePointerOffset),
        PlayerField<uint32_t>(g_player2, kPlayerStateTimerFlagsOffset),
    };
    // Original PlayerUpdate does this at 0x436DA8 before BombStartDispatch.
    // Bomb implementations therefore observe state 4 with a timer value of
    // 60, not the hit-frame timer and not a freshly zeroed active timer.
    SetPlayer2StateFrame(g_player2, 60);
    g_player2DeathbombPrepared = true;
    return true;
}

void CancelRuntimePlayer2RecoveryForDeathbomb() noexcept {
    if (!g_player2DeathbombPrepared || g_player2 == nullptr) {
        return;
    }
    PlayerField<int32_t>(g_player2, kPlayerStateOffset) = 1;
    g_player2RecoveryPhase = Player2RecoveryPhase::kActive;
    g_player2RecoveryFrame = 0;
    g_player2DeathbombActivatedThisTick = true;
    g_player2DeathbombPrepared = false;
    g_player2PreDeathbombTimer = {};
    SetPlayerRenderEnabled(g_player2, true);
    PlayerField<uint32_t>(g_player2, kPlayerAnmFlagsOffset) &= ~0x10000U;
    RefreshPlayerCollisionBounds(g_player2);
    Log(L"phase10 P2 Deathbomb committed with native state timer=60");
}

void RollbackRuntimePlayer2Deathbomb() noexcept {
    if (!g_player2DeathbombPrepared || g_player2 == nullptr) {
        return;
    }
    PlayerField<int32_t>(g_player2, kPlayerStatePreviousFrameOffset) =
        g_player2PreDeathbombTimer.previousFrame;
    PlayerField<int32_t>(g_player2, kPlayerStateFrameOffset) =
        g_player2PreDeathbombTimer.frame;
    PlayerField<float>(g_player2, kPlayerStateSubframeOffset) =
        g_player2PreDeathbombTimer.subframe;
    PlayerField<uintptr_t>(g_player2, kPlayerStateScalePointerOffset) =
        g_player2PreDeathbombTimer.scalePointer;
    PlayerField<uint32_t>(g_player2, kPlayerStateTimerFlagsOffset) =
        g_player2PreDeathbombTimer.flags;
    g_player2DeathbombPrepared = false;
    g_player2PreDeathbombTimer = {};
    Log(L"phase10 P2 Deathbomb dispatch rejected; hit timer restored");
}

int32_t ConsumeRuntimeLifeGainForRevival(int32_t gain) noexcept {
    if (gain <= 0 || g_player2 == nullptr) {
        return gain;
    }
    if (g_player1Eliminated && !g_player1RevivePending) {
        g_player1RevivePending = true;
        Log(L"phase11 shared 1UP reserved to revive P1");
        return gain - 1;
    }
    if ((g_player2RecoveryPhase == Player2RecoveryPhase::kFinalDeath ||
         g_player2RecoveryPhase == Player2RecoveryPhase::kEliminated) &&
        !g_player2RevivePending) {
        g_player2RevivePending = true;
        Log(L"phase11 shared 1UP reserved to revive P2");
        return gain - 1;
    }
    return gain;
}

int OnPlayerRenderTick(void* playerInf) noexcept {
    using RenderCallback = int (__fastcall*)(void*);
    auto original = reinterpret_cast<RenderCallback>(
        GameAddresses::kPlayerRenderCallback);
    if (playerInf == nullptr) {
        return 1;
    }

    const bool localIsPlayer2 =
        RuntimeLocalPlayerSlot() == LogicalPlayerSlot::kPlayer2;
    const bool isTeammate = localIsPlayer2
        ? playerInf != g_player2
        : playerInf == g_player2;
    const bool fade = RuntimeNetworkActive() && g_teammateFadeActive &&
                      isTeammate &&
                      PlayerField<int32_t>(playerInf, kPlayerStateOffset) == 1;
    if (!fade) {
        return original(playerInf);
    }

    // ANM scripts can rewrite the VM color between update and render. Apply
    // opacity immediately around the native draw call, then restore the exact
    // value so this local-only effect cannot tint later frames or simulation.
    auto& color = PlayerField<uint32_t>(playerInf, kPlayerAnmColorOffset);
    auto& flags = PlayerField<uint32_t>(playerInf, kPlayerAnmFlagsOffset);
    const uint32_t originalColor = color;
    const uint32_t originalFlags = flags;
    color = (static_cast<uint32_t>(kTeammateFadeAlpha) << 24U) | 0x00FFFFFFU;
    flags |= 0x10000U;
    const int result = original(playerInf);
    color = originalColor;
    flags = originalFlags;
    return result;
}

int OnPlayer1UpdateTick(void* playerInf) noexcept {
    using UpdateCallback = int (__fastcall*)(void*);
    auto original =
        reinterpret_cast<UpdateCallback>(g_originalPlayer1UpdateCallback);
    if (original == nullptr || playerInf == nullptr || g_player2 == nullptr) {
        return original != nullptr ? original(playerInf) : 1;
    }

    const uint32_t player1Input =
        *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask) & 0xFFU;
    if (!g_player2UpdatedThisTick) {
        // P1 arrived before P2 on this native update pass.  Running the
        // original callback now would advance the game without a resolved
        // P2 frame.  P2 will invoke this function after its own update.
        g_player1UpdateDeferred = true;
        g_deferredPlayer1 = playerInf;
        if (!g_loggedMissingFrame) {
            Log(L"phase7 P1 update arrived before P2; callback deferred to same frame");
            g_loggedMissingFrame = true;
        }
        return 1;
    }
    const bool framePublished = g_player2UpdatedThisTick;

    int result = 1;
    if (RuntimeResourceTransactionsEnabled() &&
        !RuntimeResourceFrameActive()) {
        BeginRuntimeResourceFrame();
    }
    CaptureRuntimeNativeResourceChanges(2);
    ExposeRuntimeResourceProjection();
    if (!RuntimeDeterminismMovementOnly() &&
        PlayerField<int32_t>(playerInf, kPlayerStateOffset) == 1) {
        const int32_t projectedPower =
            *reinterpret_cast<volatile int32_t*>(GameAddresses::kPower);
        const int32_t projectedLevel = OptionLevelForPower(projectedPower);
        if (projectedLevel != g_player1OptionLevel) {
            const int32_t previousCount = PlayerField<int32_t>(
                playerInf, kOptionCountOffset);
            const bool optionStateInvalid =
                OptionStateNeedsRepair(playerInf, projectedPower);
            // Item pickup invokes 0x4385B0 before this scheduler callback on
            // some frames. Adopt that already-complete native transition
            // instead of deleting and rebuilding the same Option set again.
            if (previousCount == projectedLevel && !optionStateInvalid) {
                g_player1OptionLevel = projectedLevel;
                Log(L"phase8 P1 Option level adopted from native power "
                    L"event before update");
            } else {
                DualResourceBanks* banks = RuntimeResourceBanks();
                const PlayerResourceBank* player1Bank =
                    banks != nullptr ? banks->Player1() : nullptr;
                if (RebuildPlayerOptions(playerInf, player1Bank,
                                         optionStateInvalid)) {
                    g_player1OptionLevel = projectedLevel;
                    std::wstringstream message;
                    message
                        << L"phase8 P1 Option synchronized before native update"
                        << L"; power=" << projectedPower
                        << L"; target_level=" << projectedLevel
                        << L"; old_count=" << previousCount;
                    Log(message.str());
                }
            }
        }
    }
    if (RuntimeDeterminismMovementOnly()) {
        const int32_t previousHorizontalVelocity =
            PlayerField<int32_t>(playerInf, kHorizontalVelocityOffset);
        auto& focusEnableTimer =
            PlayerField<int32_t>(playerInf, kFocusEnableTimerOffset);
        if (focusEnableTimer < kFocusEnableDelay) {
            focusEnableTimer = kFocusEnableDelay;
        }
        CallPlayerMovement(playerInf);
        const int32_t currentHorizontalVelocity =
            PlayerField<int32_t>(playerInf, kHorizontalVelocityOffset);
        if (ShouldRestoreIdleAnimation(previousHorizontalVelocity,
                                       currentHorizontalVelocity,
                                       player1Input)) {
            RestoreIdleAnimation(playerInf);
        }
    } else {
        const uint8_t rules = RuntimeCoopRules();
        const bool lockPower =
            coop::CoopRuleEnabled(rules, coop::kCoopRuleLockPower);
        const bool lockLives =
            coop::CoopRuleEnabled(rules, coop::kCoopRuleLockLives);
        const bool autoBomb =
            coop::CoopRuleEnabled(rules, coop::kCoopRuleAutoBomb);
        const bool infiniteRespawn =
            coop::CoopRuleEnabled(rules, coop::kCoopRuleInfiniteRespawn);
        auto& state = PlayerField<int32_t>(playerInf, kPlayerStateOffset);
        auto& stateFrame =
            PlayerField<int32_t>(playerInf, kPlayerStateFrameOffset);
        bool skipOriginal = false;

        if (autoBomb && state == 4 &&
            RuntimeProjectedResource(SharedResource::kBombs) > 0) {
            *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask) |=
                kInputBomb;
        }

        if (state == 1) {
            g_player1Eliminated = false;
            g_player1RespawnGranted = false;
            g_player1FinalDeathPending = false;
            g_player1FinalDeathFrame = 0;
        }
        if (g_player1Eliminated) {
            if (g_player1FinalDeathPending) {
                ++g_player1FinalDeathFrame;
                if (g_player1FinalDeathFrame >= kRespawnClearEntryFrame) {
                    ClearEnemyBulletsForPlayerDeath();
                    g_player1FinalDeathPending = false;
                    g_player1FinalDeathFrame = 0;
                    Log(L"phase11 P1 final death damage/clear completed at recovery frame 40");
                }
                skipOriginal = true;
            } else if (g_player1RevivePending) {
                g_player1RevivePending = false;
                g_player1Eliminated = false;
                g_player1RespawnGranted = true;
                state = 2;
                stateFrame = 30;
                Log(L"phase11 P1 revival handed to original entry path");
            } else if (g_player2RecoveryPhase ==
                       Player2RecoveryPhase::kEliminated) {
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kLives) = -1;
                Log(L"phase11 both players eliminated; original game-over path released");
            } else {
                skipOriginal = true;
            }
        } else if (state == 2 && stateFrame >= 30 &&
                   !g_player1RespawnGranted) {
            const int32_t lives =
                RuntimeProjectedResource(SharedResource::kLives);
            if (lockLives || infiniteRespawn || lives > 0) {
                if (!lockLives && lives > 0) {
                    QueueRuntimeResourceDelta(SharedResource::kLives, -1, 0);
                    ExposeRuntimeResourceProjection();
                }
                g_player1RespawnGranted = true;
                if (lockLives) {
                    Log(L"phase11 P1 respawn granted by lock-lives rule");
                } else if (lives > 0) {
                    Log(L"phase11 late shared life reserved for P1 respawn");
                } else {
                    Log(L"phase11 P1 zero-life respawn granted by infinite-respawn rule");
                }
            } else {
                const int32_t optionCount =
                    PlayerField<int32_t>(playerInf, kOptionCountOffset);
                // Native state 2 rebuilds Options after applying the death
                // Power loss because it assumes the player will enter again.
                // Co-op can keep P1 eliminated while P2 continues, so tear
                // down that rebuilt set exactly where we take ownership of
                // the original respawn path.
                DestroyPlayerOptions(playerInf);
                SpawnPlayerDeathArea(playerInf);
                g_player1Eliminated = true;
                g_player1FinalDeathPending = true;
                g_player1FinalDeathFrame = 0;
                skipOriginal = true;
                std::wstringstream message;
                message << L"phase11 P1 final death effects started before elimination"
                        << L"; rebuilt_options_removed=" << optionCount;
                Log(message.str());
            }
        }

        if (!skipOriginal) {
            const int32_t livesBefore =
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kLives);
            const bool p1DeathPowerFrame = state == 2 && stateFrame == 3;
            const int32_t powerBefore =
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kPower);
            int32_t adjustedPower = powerBefore;
            if (p1DeathPowerFrame) {
                adjustedPower = lockPower
                    ? powerBefore
                    : PowerAfterDeathLoss(powerBefore, kCoopDeathPowerLoss);
                // Native PlayerUpdate subtracts 1.00P and immediately rebuilds
                // Options in this callback. Feed it a compensated value so its
                // single rebuild lands directly on the co-op 0.50P result.
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kPower) =
                    adjustedPower + 100;
                if (lockLives || (infiniteRespawn && livesBefore <= 0)) {
                    // Native state 2 decides between respawn and game over in
                    // the same settlement call that consumes a life. Give it
                    // one temporary life so lock-lives or a zero-stock
                    // infinite respawn runs the normal entry path; the exact
                    // shared value is restored
                    // before resource-change capture observes this callback.
                    *reinterpret_cast<volatile int32_t*>(GameAddresses::kLives) =
                        livesBefore + 1;
                }
            }
            result = original(playerInf);
            if (p1DeathPowerFrame) {
                // Reassert the exact projected value in case a character path
                // clamps the compensated input differently.
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kPower) =
                    adjustedPower;
                g_loggedOptionRebuild = false;
                std::wstringstream message;
                message << L"phase11 P1 death Power adjusted; "
                        << powerBefore << L"->" << adjustedPower
                        << L"; loss="
                        << (lockPower ? 0 : kCoopDeathPowerLoss)
                        << L"; floor=100; lock_power="
                        << (lockPower ? 1 : 0);
                Log(message.str());
            }
            const int32_t livesAfter =
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kLives);
            const bool preserveNativeLife =
                lockLives || (infiniteRespawn && livesBefore <= 0);
            if (preserveNativeLife && p1DeathPowerFrame) {
                *reinterpret_cast<volatile int32_t*>(GameAddresses::kLives) =
                    livesBefore;
                g_player1RespawnGranted = true;
                Log(lockLives
                        ? L"phase11 P1 native life settlement bypassed by lock-lives rule"
                        : L"phase11 P1 zero-life game-over bypassed by infinite-respawn rule");
            } else if (livesAfter < livesBefore) {
                g_player1RespawnGranted = livesBefore > 0;
            }
        } else {
            // The scheduler keeps the P1 PlayerInf alive while P2 continues.
            // Freezing the full object here also freezes its player bullets and
            // attack-area pool, even though enemy damage scans still see that
            // memory. Keep only those two lifetimes moving; no new P1 shots are
            // dispatched while the player is eliminated.
            CallPlayerBulletUpdate(playerInf);
            TickPlayer2DamageAreas(playerInf);
        }
    }

    if (!RuntimeDeterminismMovementOnly() &&
        PlayerField<int32_t>(playerInf, kPlayerStateOffset) == 1) {
        const int32_t power = *reinterpret_cast<volatile int32_t*>(
            GameAddresses::kPower);
        const int32_t optionLevel = OptionLevelForPower(power);
        const int32_t optionCount = PlayerField<int32_t>(
            playerInf, kOptionCountOffset);
        const bool optionStateInvalid =
            OptionStateNeedsRepair(playerInf, power);
        const bool levelChanged = optionLevel != g_player1OptionLevel;
        const uint32_t currentFrame = RuntimeFrameNumber();
        const bool repairCooldownElapsed =
            currentFrame == 0 || g_lastP1OptionRepairFrame == 0 ||
            currentFrame - g_lastP1OptionRepairFrame >= 30;
        if (levelChanged && optionCount == optionLevel &&
            !optionStateInvalid) {
            g_player1OptionLevel = optionLevel;
            Log(L"phase8 P1 Option level already synchronized by native item/death path");
        } else if (levelChanged ||
                   (optionStateInvalid && repairCooldownElapsed)) {
            DualResourceBanks* banks = RuntimeResourceBanks();
            const PlayerResourceBank* player1Bank =
                banks != nullptr ? banks->Player1() : nullptr;
            if (RebuildPlayerOptions(playerInf, player1Bank,
                                     optionStateInvalid)) {
                g_player1OptionLevel = optionLevel;
                if (optionStateInvalid) {
                    g_lastP1OptionRepairFrame = currentFrame;
                }
                std::wstringstream message;
                message << L"phase8 P1 Option synchronized; power=" << power
                        << L"; target_level=" << optionLevel
                        << L"; old_count=" << optionCount
                        << L"; integrity_repair="
                        << (optionStateInvalid ? 1 : 0);
                Log(message.str());
            }
        }
    }
    ApplyTeammateFade(playerInf, g_player2);

    FrameInput completedFrame{};
    const bool frameCompleted =
        framePublished && ConsumeRuntimeFrameInput(completedFrame);
    if (frameCompleted && g_player2 != nullptr) {
        QueueRuntimeEndOfFrameDeterminism(completedFrame, playerInf, g_player2);
    }
    CaptureRuntimeNativeResourceChanges(0);
    g_player2UpdatedThisTick = false;
    return result;
}

int OnPlayer2UpdateTick(void* playerInf) noexcept {
    if (playerInf == nullptr || playerInf != g_player2 || !g_enabled) {
        return 1;
    }
    PlayerContextManager* contexts = RuntimePlayerContexts();
    if (contexts == nullptr || contexts->ActiveSlot() != PlayerSlot::kPlayer1) {
        return 1;
    }
    const uint32_t nativeInput = RuntimePhysicalPlayer1Input();
    const uint32_t localPlayer2Input = ReadPlayer2Input();
    FrameInput resolvedInput{};
    if (!BeginRuntimeFrameInput(nativeInput, localPlayer2Input,
                                resolvedInput)) {
        return 1;
    }
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask) =
        resolvedInput.player1Mask;
    if (!contexts->SnapshotActive()) {
        FrameInput discarded{};
        ConsumeRuntimeFrameInput(discarded);
        return 1;
    }
    const PlayerContext* stored = contexts->GetContext(PlayerSlot::kPlayer2);
    const PlayerContext* player1 = contexts->GetContext(PlayerSlot::kPlayer1);
    if (stored == nullptr || player1 == nullptr) {
        FrameInput discarded{};
        ConsumeRuntimeFrameInput(discarded);
        return 1;
    }

    CaptureRuntimeNativeResourceChanges(0);
    CommitRuntimeResourceFrame();
    BeginRuntimeResourceFrame();

    uint32_t inputMask = resolvedInput.player2Mask;
    if (coop::CoopRuleEnabled(RuntimeCoopRules(),
                              coop::kCoopRuleAutoBomb) &&
        PlayerField<int32_t>(playerInf, kPlayerStateOffset) == 4 &&
        RuntimeProjectedResource(SharedResource::kBombs) > 0) {
        inputMask |= kInputBomb;
    }
    PlayerContext updated = *stored;
    updated.inputMask = inputMask;
    if (!contexts->ConfigureInactive(PlayerSlot::kPlayer2, updated)) {
        FrameInput discarded{};
        ConsumeRuntimeFrameInput(discarded);
        return 1;
    }

    const int32_t beforeX = PlayerField<int32_t>(playerInf, kFixedPositionXOffset);
    const int32_t beforeY = PlayerField<int32_t>(playerInf, kFixedPositionYOffset);
    const int32_t previousHorizontalVelocity =
        PlayerField<int32_t>(playerInf, kHorizontalVelocityOffset);
    auto& focusEnableTimer =
        PlayerField<int32_t>(playerInf, kFocusEnableTimerOffset);
    if (focusEnableTimer < kFocusEnableDelay) {
        focusEnableTimer = kFocusEnableDelay;
    }
    {
        ScopedPlayerContext playerScope(*contexts, PlayerSlot::kPlayer2);
        if (!playerScope.IsActive()) {
            FrameInput discarded{};
            ConsumeRuntimeFrameInput(discarded);
            return 1;
        }
        UpdateRuntimePlayer2Bomb(inputMask, playerInf);
        const bool recovering =
            !RuntimeDeterminismMovementOnly() &&
            UpdatePlayer2Recovery(playerInf);
        if (!RuntimeDeterminismMovementOnly()) {
            // P2 does not run the original full PlayerUpdate callback, whose
            // tail advances this pool. Advance it exactly once here so death
            // and Bomb damage areas grow, deal damage, and expire normally.
            TickPlayer2DamageAreas(playerInf);
        }
        if (recovering) {
            if (g_combatEnabled) {
                DualResourceBanks* banks = RuntimeResourceBanks();
                const PlayerResourceBank* player2Bank =
                    banks != nullptr ? banks->Player2() : nullptr;
                void** anmSlot = PlayerAnmSlot();
                if (player2Bank != nullptr && anmSlot != nullptr) {
                     ScopedPointerValue slotScope(anmSlot,
                                                  player2Bank->anmArchive);
                     ScopedPlayer2Airframe airframeScope(
                         player2Bank->airframeIndex);
                     CallPlayerBulletUpdate(playerInf);
                    if (g_player2RecoveryPhase !=
                            Player2RecoveryPhase::kDeathDelay ||
                        g_player2RecoveryFrame >= kHitVisualFrames) {
                        DestroyPlayerOptions(playerInf);
                    }
                }
            }
        } else if (!g_player2DeathbombActivatedThisTick) {
            CallPlayerMovement(playerInf);
            const int32_t currentHorizontalVelocity =
                PlayerField<int32_t>(playerInf, kHorizontalVelocityOffset);
            if (ShouldRestoreIdleAnimation(previousHorizontalVelocity,
                                           currentHorizontalVelocity, inputMask)) {
                RestoreIdleAnimation(playerInf);
            }

            if (g_combatEnabled && !RuntimeDeterminismMovementOnly()) {
                DualResourceBanks* banks = RuntimeResourceBanks();
                const PlayerResourceBank* player2Bank =
                    banks != nullptr ? banks->Player2() : nullptr;
                void** anmSlot = PlayerAnmSlot();
                if (player2Bank == nullptr || anmSlot == nullptr ||
                    PlayerField<void*>(playerInf,
                                       GameAddresses::kPlayerAnmPointerOffset) !=
                        player2Bank->anmArchive ||
                    PlayerField<void*>(playerInf,
                                       GameAddresses::kPlayerShtPointerOffset) !=
                        player2Bank->shtData) {
                    if (!g_loggedCombatUnavailable) {
                        Log(L"phase8 P2 combat update skipped: resource binding unavailable");
                        g_loggedCombatUnavailable = true;
                    }
                } else {
                    ScopedPointerValue slotScope(anmSlot, player2Bank->anmArchive);
                    ScopedPlayer2Airframe airframeScope(
                        player2Bank->airframeIndex);
                    const int32_t power = *reinterpret_cast<volatile int32_t*>(
                        GameAddresses::kPower);
                    const int32_t optionLevel = OptionLevelForPower(power);
                    const bool optionStateInvalid =
                        OptionStateNeedsRepair(playerInf, power);
                    const uint32_t currentFrame = RuntimeFrameNumber();
                    const bool repairCooldownElapsed =
                        currentFrame == 0 || g_lastP2OptionRepairFrame == 0 ||
                        currentFrame - g_lastP2OptionRepairFrame >= 30;
                    const bool levelChanged =
                        optionLevel != g_player2OptionLevel;
                    const int32_t currentOptionCount = PlayerField<int32_t>(
                        playerInf, kOptionCountOffset);
                    if (levelChanged && currentOptionCount == optionLevel &&
                        !optionStateInvalid) {
                        g_player2OptionLevel = optionLevel;
                        Log(L"phase8 P2 Option level already synchronized by native item path");
                    } else if (levelChanged ||
                               (optionStateInvalid && repairCooldownElapsed)) {
                        const int32_t previousLevel = g_player2OptionLevel;
                        const int32_t previousCount = currentOptionCount;
                        const bool rebuilt = RebuildPlayerOptions(
                            playerInf, player2Bank, optionStateInvalid);
                        if (!rebuilt) {
                            if (!g_loggedCombatUnavailable) {
                                Log(L"phase8 P2 Option rebuild skipped: ANM slot unavailable");
                                g_loggedCombatUnavailable = true;
                            }
                        } else {
                            g_player2OptionLevel = optionLevel;
                            if (optionStateInvalid && repairCooldownElapsed) {
                                g_lastP2OptionRepairFrame = currentFrame;
                                Log(L"phase8 P2 Option integrity repair applied");
                            }
                            std::wstringstream message;
                            message
                                << L"phase8 P2 Option rebuilt; power=" << power
                                << L"; level=" << previousLevel << L"->"
                                << optionLevel << L"; old_count="
                                << previousCount << L"; airframe="
                                << player2Bank->airframeIndex
                                << L"; option_count="
                                << PlayerField<int32_t>(playerInf,
                                                        kOptionCountOffset)
                                << L"; option0_active="
                                << PlayerField<uint32_t>(
                                       playerInf, kOptionActiveOffset)
                                << L"; option0_vm="
                                << PlayerField<uint32_t>(
                                       playerInf, kOptionAnmIdOffset)
                                << L"/"
                                << PlayerField<uint32_t>(
                                       playerInf, kOptionAnmIdOffset + 4);
                            Log(message.str());
                            g_loggedOptionRebuild = true;
                        }
                    }

                    const int32_t shotTimerBefore =
                        PlayerField<int32_t>(playerInf, kShotTimerOffset);
                    const int32_t shotSequenceBefore =
                        PlayerField<int32_t>(playerInf, kShotSequenceOffset);
                    if (!g_loggedShootInput && (inputMask & kInputShoot) != 0) {
                        std::wstringstream message;
                        message << L"phase8 P2 shoot input observed; state="
                                << PlayerField<int32_t>(playerInf,
                                                        kPlayerStateOffset)
                                << L"; timer=" << shotTimerBefore
                                << L"; sequence=" << shotSequenceBefore;
                        Log(message.str());
                        g_loggedShootInput = true;
                    }
                    CallPlayerShotUpdate(playerInf);
                    CallPlayerBulletUpdate(playerInf);
                    if (!g_loggedShot && (inputMask & kInputShoot) != 0 &&
                        (shotTimerBefore !=
                             PlayerField<int32_t>(playerInf, kShotTimerOffset) ||
                         shotSequenceBefore !=
                             PlayerField<int32_t>(playerInf,
                                                  kShotSequenceOffset))) {
                        std::wstringstream message;
                        message << L"phase8 P2 shot observed; timer="
                                << shotTimerBefore << L"->"
                                << PlayerField<int32_t>(playerInf, kShotTimerOffset)
                                << L"; sequence=" << shotSequenceBefore << L"->"
                                << PlayerField<int32_t>(playerInf,
                                                        kShotSequenceOffset);
                        Log(message.str());
                        g_loggedShot = true;
                    }
                }
            }
        }
        if (!RuntimeDeterminismMovementOnly() &&
            PlayerField<int32_t>(playerInf, kPlayerStateOffset) == 1) {
            TickPlayerInvincibility(playerInf);
            RefreshPlayerCollisionBounds(playerInf);
        }
        if (PlayerField<int32_t>(playerInf, kPlayerStateOffset) == 1) {
            AdvancePlayer2StateFrame(playerInf);
        }
    }
    // Deathbomb acceptance deliberately skips movement and shooting for its
    // native acceptance frame. Clear the guard outside that branch so it can
    // never latch and suppress all later active-player updates.
    g_player2DeathbombActivatedThisTick = false;

    if (RuntimeLocalPlayerSlot() != LogicalPlayerSlot::kPlayer2) {
        // P2 does not run the complete native PlayerUpdate callback. Remove its
        // teammate-only hitbox as soon as the private update finishes, before
        // any later scheduler/render registration can draw it.
        SetTeammateHitboxHidden(playerInf, true);
    }

    const int32_t afterX = PlayerField<int32_t>(playerInf, kFixedPositionXOffset);
    const int32_t afterY = PlayerField<int32_t>(playerInf, kFixedPositionYOffset);
    const bool focusRequested = (inputMask & kInputFocus) != 0;
    const bool focusActive =
        PlayerField<int32_t>(playerInf, kFocusActiveOffset) != 0;
    if (!g_loggedMovement && (inputMask & kInputMovementMask) != 0 &&
        (beforeX != afterX || beforeY != afterY)) {
        std::wstringstream message;
        message << L"phase5 P2 movement observed; focus_active="
                << (focusActive ? 1 : 0)
                << L"; next_frame=" << (RuntimeFrameNumber() + 1)
                << L"; fixed_from=(" << beforeX << L"," << beforeY << L")"
                << L"; fixed_to=(" << afterX << L"," << afterY << L")";
        Log(message.str());
        g_loggedMovement = true;
    }
    if (!g_loggedFocus && focusRequested && focusActive) {
        Log(L"phase5 P2 focus movement observed; active=1");
        g_loggedFocus = true;
    }
    if (!g_loggedBoundary &&
        (afterX == kMinimumFixedX || afterX == kMaximumFixedX ||
         afterY == kMinimumFixedY || afterY == kMaximumFixedY)) {
        std::wstringstream message;
        message << L"phase5 P2 boundary clamp observed; fixed_position=("
                << afterX << L"," << afterY << L")";
        Log(message.str());
        g_loggedBoundary = true;
    }
    g_player2AppliedInput = inputMask;
    g_player2UpdatedThisTick = true;
    if (g_player1UpdateDeferred && g_deferredPlayer1 != nullptr) {
        // Complete the deferred P1 callback while the P2 frame is still
        // latched.  OnPlayer1UpdateTick consumes the frame and clears the
        // per-tick P2 marker, exactly as in the normal P2-then-P1 order.
        void* deferredPlayer1 = g_deferredPlayer1;
        g_deferredPlayer1 = nullptr;
        g_player1UpdateDeferred = false;
        OnPlayer1UpdateTick(deferredPlayer1);
    }
    return 1;
}

}  // namespace coop::th12
