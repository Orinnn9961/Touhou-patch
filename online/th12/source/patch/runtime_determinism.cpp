#include "runtime_determinism.h"

#include "determinism_trace.h"
#include "log.h"
#include "memory_patch.h"
#include "path_util.h"
#include "runtime_bomb.h"
#include "runtime_boss.h"
#include "runtime_collision.h"
#include "runtime_frame_input.h"
#include "runtime_player2.h"
#include "version_map.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <sstream>

extern "C" void __stdcall Phase7CaptureEndOfFrame();

extern "C" __declspec(naked) int Phase7EndOfFrameUpdateHook() {
    __asm {
        mov eax, 004624C0h
        call eax
        pushfd
        pushad
        call Phase7CaptureEndOfFrame
        popad
        popfd
        ret
    }
}

namespace coop::th12 {
namespace {

constexpr uint32_t kFixedPositionXOffset = 0x988;
constexpr uint32_t kFixedPositionYOffset = 0x98C;
constexpr uint32_t kFocusActiveOffset = 0xC598;
constexpr uint32_t kPlayerStateOffset = 0xA28;
constexpr uint32_t kPlayerStateFrameOffset = 0xA34;
constexpr uint32_t kInvincibilityTimerOffset = 0xC404;
constexpr uint32_t kShotTimerOffset = 0xC420;
constexpr uint32_t kShotSequenceOffset = 0xC424;
constexpr uint32_t kOptionCountOffset = 0xC41C;
constexpr uint32_t kOptionActiveOffset = 0x8260;
constexpr uint32_t kOptionStride = 0xE4;
constexpr uint32_t kOptionCapacity = 8;
constexpr uint32_t kPlayerBulletPoolOffset = 0xA58;
constexpr uint32_t kPlayerBulletStride = 0x78;
constexpr uint32_t kPlayerBulletCapacity = 0x100;
constexpr uint32_t kPlayerBulletStateOffset = 0x48;
constexpr uint32_t kPlayerBulletDamageOffset = 0x60;
constexpr uint32_t kDamageAreaPoolOffset = 0x8988;
constexpr uint32_t kDamageAreaStride = 0x74;
constexpr uint32_t kDamageAreaCapacity = 0x80;
constexpr uint32_t kBombActiveOffset = 0x3C;
constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

std::array<RelativeCallPatch, GameAddresses::kGameplayUpdateCallSites.size()>
    g_endOfFramePatches{
        RelativeCallPatch(GameAddresses::kGameplayUpdateCallSites[0],
                          GameAddresses::kUpdateScheduler),
        RelativeCallPatch(GameAddresses::kGameplayUpdateCallSites[1],
                          GameAddresses::kUpdateScheduler),
        RelativeCallPatch(GameAddresses::kGameplayUpdateCallSites[2],
                          GameAddresses::kUpdateScheduler),
    };

std::wstring g_root;
std::wstring g_traceDirectory;
HANDLE g_trace = INVALID_HANDLE_VALUE;
uint32_t g_samplesWritten = 0;
uint32_t g_traceSequence = 0;
bool g_configured = false;
bool g_enabled = false;
bool g_traceEnabled = false;
bool g_movementOnly = false;
bool g_loggedWriteFailure = false;
uint32_t g_lastFrame = 0;
uint64_t g_lastHash = 0;
DeterminismSample g_lastSample{};
FrameInput g_pendingInput{};
void* g_pendingPlayer1 = nullptr;
void* g_pendingPlayer2 = nullptr;
bool g_pendingEndOfFrame = false;

struct CombatSummary {
    uint64_t hash{kFnvOffset};
    uint32_t bullets{};
    uint32_t options{};
    uint32_t damageAreas{};
};

template <typename T>
T PlayerField(const void* playerInf, uint32_t offset) noexcept {
    return *reinterpret_cast<const T*>(
        static_cast<const uint8_t*>(playerInf) + offset);
}

void Log(const std::wstring& message) noexcept {
    if (!g_root.empty()) {
        coop::WriteLog(g_root, L"patch.log", message);
    }
}

bool WriteAll(const void* data, DWORD size) noexcept {
    DWORD written = 0;
    return g_trace != INVALID_HANDLE_VALUE &&
           WriteFile(g_trace, data, size, &written, nullptr) != 0 &&
           written == size;
}

void Hash32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kFnvPrime;
    }
}

void Hash64(uint64_t& hash, uint64_t value) noexcept {
    Hash32(hash, static_cast<uint32_t>(value));
    Hash32(hash, static_cast<uint32_t>(value >> 32));
}

CombatSummary SummarizePlayerCombat(const void* playerInf) noexcept {
    CombatSummary summary{};
    if (playerInf == nullptr) {
        return summary;
    }
    Hash32(summary.hash, PlayerField<uint32_t>(playerInf, kShotTimerOffset));
    Hash32(summary.hash, PlayerField<uint32_t>(playerInf, kShotSequenceOffset));
    Hash32(summary.hash, PlayerField<uint32_t>(playerInf, kOptionCountOffset));

    auto* bullet = static_cast<const uint8_t*>(playerInf) +
                   kPlayerBulletPoolOffset;
    for (uint32_t index = 0; index < kPlayerBulletCapacity;
         ++index, bullet += kPlayerBulletStride) {
        const uint32_t state = PlayerField<uint32_t>(
            bullet, kPlayerBulletStateOffset);
        if (state == 0 || state == 2) {
            continue;
        }
        ++summary.bullets;
        Hash32(summary.hash, index);
        Hash32(summary.hash, state);
        Hash32(summary.hash, PlayerField<uint32_t>(bullet, 0x00));
        Hash32(summary.hash, PlayerField<uint32_t>(bullet, 0x04));
        Hash32(summary.hash, PlayerField<uint32_t>(bullet, 0x08));
        Hash32(summary.hash, PlayerField<uint32_t>(
            bullet, kPlayerBulletDamageOffset));
    }

    for (uint32_t index = 0; index < kOptionCapacity; ++index) {
        const uint32_t active = PlayerField<uint32_t>(
            playerInf, kOptionActiveOffset + index * kOptionStride);
        if (active == 0) {
            continue;
        }
        ++summary.options;
        Hash32(summary.hash, index);
        Hash32(summary.hash, active);
    }

    auto* area = static_cast<const uint8_t*>(playerInf) +
                 kDamageAreaPoolOffset;
    for (uint32_t index = 0; index < kDamageAreaCapacity;
         ++index, area += kDamageAreaStride) {
        const uint32_t flags = PlayerField<uint32_t>(area, 0x70);
        if ((flags & 1U) == 0) {
            continue;
        }
        ++summary.damageAreas;
        Hash32(summary.hash, index);
        Hash32(summary.hash, flags);
        Hash32(summary.hash, PlayerField<uint32_t>(area, 0x00));
        Hash32(summary.hash, PlayerField<uint32_t>(area, 0x08));
        Hash32(summary.hash, PlayerField<uint32_t>(area, 0x50));
        Hash32(summary.hash, PlayerField<uint32_t>(area, 0x54));
    }
    return summary;
}

uint32_t ReadBombState(const void* manager) noexcept {
    return manager != nullptr
        ? PlayerField<uint32_t>(manager, kBombActiveOffset)
        : 0U;
}

uint64_t HashCollisionCounters() noexcept {
    uint64_t hash = kFnvOffset;
    const Phase9Counters& counters = RuntimePhase9Counters();
    for (uint32_t player = 0; player < 2; ++player) {
        Hash64(hash, counters.hits[player]);
        Hash64(hash, counters.grazes[player]);
        Hash64(hash, counters.itemContacts[player]);
        Hash64(hash, counters.damageScans[player]);
    }
    Hash64(hash, RuntimeBossDamageScaleHash());
    return hash;
}

bool InstallEndOfFramePatches(std::wstring& error) noexcept {
    for (auto& patch : g_endOfFramePatches) {
        if (!patch.Install(reinterpret_cast<void*>(&Phase7EndOfFrameUpdateHook))) {
            for (auto& installed : g_endOfFramePatches) {
                installed.Restore();
            }
            error = L"phase7 global end-of-frame call-site signature mismatch";
            return false;
        }
    }
    return true;
}

}  // namespace

GameRngState ReadRuntimeGameRngState() noexcept {
    GameRngState state{};
    state.primary.seed = *reinterpret_cast<volatile const uint16_t*>(
        GameAddresses::kGameRngPrimary);
    state.primary.calls = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kGameRngPrimary + 4);
    state.secondary.seed = *reinterpret_cast<volatile const uint16_t*>(
        GameAddresses::kGameRngSecondary);
    state.secondary.calls = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kGameRngSecondary + 4);
    return state;
}

void WriteRuntimeGameRngState(const GameRngState& state) noexcept {
    *reinterpret_cast<volatile uint16_t*>(GameAddresses::kGameRngPrimary) =
        state.primary.seed;
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kGameRngPrimary + 4) =
        state.primary.calls;
    *reinterpret_cast<volatile uint16_t*>(GameAddresses::kGameRngSecondary) =
        state.secondary.seed;
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kGameRngSecondary + 4) =
        state.secondary.calls;
}

bool InitializeRuntimeDeterminism(const std::wstring& root,
                                  std::wstring& error) noexcept {
    if (g_configured) {
        return true;
    }
    g_root = root;
    const std::wstring configPath = root + L"\\coop\\config.ini";
    g_enabled = GetPrivateProfileIntW(L"phase7", L"enabled", 1,
                                      configPath.c_str()) != 0;
    g_traceEnabled = GetPrivateProfileIntW(L"phase7", L"trace", 1,
                                           configPath.c_str()) != 0;
    g_movementOnly = GetPrivateProfileIntW(
                         L"phase7", L"movement_only_test", 0,
                         configPath.c_str()) != 0;
    if (!g_enabled) {
        g_configured = true;
        Log(L"phase7 determinism verification disabled by config");
        return true;
    }

    wchar_t directory[MAX_PATH]{};
    GetPrivateProfileStringW(L"phase7", L"trace_directory", L"coop\\logs",
                             directory, static_cast<DWORD>(std::size(directory)),
                             configPath.c_str());
    if (directory[0] == L'\0' || directory[0] == L'\\' || directory[0] == L'/' ||
        (directory[1] == L':' && directory[2] != L'\0')) {
        error = L"phase7.trace_directory must be a non-empty relative path";
        return false;
    }
    g_traceDirectory = coop::JoinPath(root, directory);
    if (g_traceEnabled && !coop::EnsureDirectory(g_traceDirectory)) {
        error = L"unable to create phase7 trace directory";
        return false;
    }
    if (!InstallEndOfFramePatches(error)) {
        return false;
    }
    g_configured = true;
    std::wstringstream message;
    message << (g_traceEnabled
                    ? L"phase7 determinism trace armed"
                    : L"phase7 determinism sampling armed; trace output disabled")
            << L"; rng=th12_primary_secondary_full_state"
            << L"; sample_anchor=global_update_chain_exit"
            << L"; movement_only_test=" << (g_movementOnly ? 1 : 0);
    Log(message.str());
    return true;
}

void FinishRuntimeDeterminismTrace() noexcept {
    if (g_trace == INVALID_HANDLE_VALUE) {
        return;
    }
    FlushFileBuffers(g_trace);
    CloseHandle(g_trace);
    g_trace = INVALID_HANDLE_VALUE;
    std::wstringstream message;
    message << L"phase7 determinism trace closed; samples=" << g_samplesWritten;
    Log(message.str());
}

void ResetRuntimeDeterminismTrace() noexcept {
    FinishRuntimeDeterminismTrace();
    g_samplesWritten = 0;
    g_lastFrame = 0;
    g_lastHash = 0;
    g_lastSample = {};
    g_pendingInput = {};
    g_pendingPlayer1 = nullptr;
    g_pendingPlayer2 = nullptr;
    g_pendingEndOfFrame = false;
    g_loggedWriteFailure = false;
    if (!g_enabled || !g_traceEnabled) {
        return;
    }

    std::wstringstream filename;
    ++g_traceSequence;
    filename << L"phase7-trace-" << GetCurrentProcessId() << L"-"
             << g_traceSequence << L".bin";
    const std::wstring path = coop::JoinPath(g_traceDirectory, filename.str().c_str());
    g_trace = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto header = EncodeDeterminismTraceHeader();
    if (g_trace == INVALID_HANDLE_VALUE ||
        !WriteAll(header.data(), static_cast<DWORD>(header.size()))) {
        if (g_trace != INVALID_HANDLE_VALUE) {
            CloseHandle(g_trace);
            g_trace = INVALID_HANDLE_VALUE;
        }
        Log(L"phase7 unable to create determinism trace");
        return;
    }
    Log(L"phase7 determinism trace opened: " + path);
}

bool CaptureRuntimeDeterminism(const FrameInput& input, void* player1,
                               void* player2) noexcept {
    if (!g_enabled || player1 == nullptr || player2 == nullptr || input.frame == 0) {
        return false;
    }
    DeterminismSample sample{};
    sample.frame = input.frame;
    sample.player1Input = input.player1Mask;
    sample.player2Input = input.player2Mask;
    sample.player1X = PlayerField<int32_t>(player1, kFixedPositionXOffset);
    sample.player1Y = PlayerField<int32_t>(player1, kFixedPositionYOffset);
    sample.player2X = PlayerField<int32_t>(player2, kFixedPositionXOffset);
    sample.player2Y = PlayerField<int32_t>(player2, kFixedPositionYOffset);
    sample.player1Focus =
        PlayerField<int32_t>(player1, kFocusActiveOffset) != 0 ? 1U : 0U;
    sample.player2Focus =
        PlayerField<int32_t>(player2, kFocusActiveOffset) != 0 ? 1U : 0U;
    sample.player1State = PlayerField<uint32_t>(player1, kPlayerStateOffset);
    sample.player1StateFrame =
        PlayerField<uint32_t>(player1, kPlayerStateFrameOffset);
    sample.player1Invincibility =
        PlayerField<uint32_t>(player1, kInvincibilityTimerOffset);
    sample.player2State = PlayerField<uint32_t>(player2, kPlayerStateOffset);
    sample.player2StateFrame =
        PlayerField<uint32_t>(player2, kPlayerStateFrameOffset);
    sample.player2Invincibility =
        PlayerField<uint32_t>(player2, kInvincibilityTimerOffset);
    sample.player2RecoveryPhase = RuntimePlayer2RecoveryState();
    sample.power = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kPower);
    sample.lives = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kLives);
    sample.bombs = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kBombs);
    sample.lifeFragments = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kLifeFragments);
    sample.bombFragments = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kBombFragments);
    sample.ufo0 = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kUfo0);
    sample.ufo1 = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kUfo1);
    sample.ufo2 = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kUfo2);
    sample.ufoState = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kUfoState);
    sample.ufoFlags = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kUfoFlags);
    sample.score = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kScore);
    sample.pointValue = *reinterpret_cast<volatile const uint32_t*>(
        GameAddresses::kPointValue);
    sample.gameRng = ReadRuntimeGameRngState();
    sample.gameRngHash = HashGameRngState(sample.gameRng);
    const CombatSummary player1Combat = SummarizePlayerCombat(player1);
    const CombatSummary player2Combat = SummarizePlayerCombat(player2);
    sample.player1CombatHash = player1Combat.hash;
    sample.player2CombatHash = player2Combat.hash;
    sample.player1ActiveBullets = player1Combat.bullets;
    sample.player2ActiveBullets = player2Combat.bullets;
    sample.player1ActiveOptions = player1Combat.options;
    sample.player2ActiveOptions = player2Combat.options;
    sample.player1ActiveDamageAreas = player1Combat.damageAreas;
    sample.player2ActiveDamageAreas = player2Combat.damageAreas;
    sample.player1BombState = ReadBombState(
        *reinterpret_cast<void* volatile*>(GameAddresses::kBombManager));
    sample.player2BombState = ReadBombState(RuntimePlayer2BombManager());
    sample.collisionHash = HashCollisionCounters();
    sample.stateHash = HashDeterminismSample(sample);
    g_lastSample = sample;
    g_lastFrame = sample.frame;
    g_lastHash = sample.stateHash;

    if (g_trace == INVALID_HANDLE_VALUE) {
        return true;
    }
    const auto bytes = EncodeDeterminismSample(sample);
    if (!WriteAll(bytes.data(), static_cast<DWORD>(bytes.size()))) {
        if (!g_loggedWriteFailure) {
            Log(L"phase7 trace write failed; determinism sampling continues");
            g_loggedWriteFailure = true;
        }
        return false;
    }
    ++g_samplesWritten;
    return true;
}

void QueueRuntimeEndOfFrameDeterminism(const FrameInput& input, void* player1,
                                       void* player2) noexcept {
    if (!g_enabled || input.frame == 0 || player1 == nullptr || player2 == nullptr) {
        return;
    }
    // Full combat hashing scans both player bullet pools. Keep per-frame
    // samples only for explicit trace captures; normal network play samples
    // exactly on the configured state-check frames.
    if (!g_traceEnabled && !RuntimeStateHashDue(input.frame)) {
        return;
    }
    g_pendingInput = input;
    g_pendingPlayer1 = player1;
    g_pendingPlayer2 = player2;
    g_pendingEndOfFrame = true;
}

void CompleteRuntimeEndOfFrameDeterminism() noexcept {
    if (!g_pendingEndOfFrame) {
        return;
    }
    const FrameInput input = g_pendingInput;
    void* player1 = g_pendingPlayer1;
    void* player2 = g_pendingPlayer2;
    g_pendingEndOfFrame = false;
    g_pendingPlayer1 = nullptr;
    g_pendingPlayer2 = nullptr;
    if (CaptureRuntimeDeterminism(input, player1, player2) &&
        RuntimeLastDeterminismFrame() == input.frame) {
        SubmitRuntimeStateHash(input.frame, RuntimeLastDeterminismHash());
    }
}

uint32_t RuntimeLastDeterminismFrame() noexcept {
    return g_lastFrame;
}

uint64_t RuntimeLastDeterminismHash() noexcept {
    return g_lastHash;
}

bool RuntimeLastDeterminismSample(DeterminismSample& sample) noexcept {
    if (g_lastFrame == 0) {
        return false;
    }
    sample = g_lastSample;
    return true;
}

bool RuntimeDeterminismMovementOnly() noexcept {
    return g_enabled && g_movementOnly;
}

}  // namespace coop::th12

extern "C" void __stdcall Phase7CaptureEndOfFrame() {
    coop::th12::CompleteRuntimeEndOfFrameDeterminism();
}
