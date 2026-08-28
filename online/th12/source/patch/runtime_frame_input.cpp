#include "runtime_frame_input.h"

#include "coop_rules.h"
#include "lan_session.h"
#include "lockstep_timeline.h"
#include "log.h"
#include "memory_patch.h"
#include "network_protocol.h"
#include "runtime_determinism.h"
#include "script_input.h"
#include "sha256.h"
#include "version_map.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <string>

namespace coop::th12 {
namespace {

enum class RuntimeNetworkMode : uint8_t {
    kLocal,
    kHost,
    kClient,
};

FrameInputClock g_clock;
FrameInputBuffer g_localFrames;
FrameInputBuffer g_remoteFrames;
FrameInputLatch g_frameLatch;
LockstepTimeline g_lockstep;
LanInputSession g_session;
CodePatch g_replayInputCallbackPatch;
std::array<uint32_t, FrameInputBuffer::kCapacity> g_hashFrames{};
std::array<uint64_t, FrameInputBuffer::kCapacity> g_hashes{};
std::array<bool, FrameInputBuffer::kCapacity> g_hashValid{};
std::wstring g_root;
std::wstring g_pendingFatal;
RuntimeNetworkMode g_mode = RuntimeNetworkMode::kLocal;
LogicalPlayerSlot g_localSlot = LogicalPlayerSlot::kPlayer1;
uint8_t g_inputDelay = 2;
uint8_t g_inputRedundancy = 8;
uint8_t g_coopRules = 0;
bool g_adaptiveDelay = true;
uint8_t g_player1Airframe = 0xFF;
uint8_t g_player2Airframe = 0xFF;
uint32_t g_connectTimeoutMs = 15000;
uint32_t g_disconnectTimeoutMs = 15000;
uint32_t g_inputStallTimeoutMs = 30000;
uint32_t g_stateCheckInterval = 600;
uint32_t g_nativePhysicalInput = 0;
uint32_t g_synchronizedInput = 0;
uint32_t g_previousSynchronizedInput = 0;
uint32_t g_pauseKey = VK_PAUSE;
uint32_t g_lastHashFrame = 0;
uint64_t g_lastHash = 0;
uint32_t g_timeline = 0;
uint64_t g_runtimeBinarySignature = 0;
GameRngState g_lobbyGameRng{};
bool g_configured = false;
bool g_ready = false;
bool g_lobbySelectionsReady = false;
bool g_lobbyResourcesReady = false;
bool g_lobbyStarted = false;
bool g_lobbyGameRngReady = false;
bool g_localPaused = false;
bool g_pauseKeyDown = false;
uint32_t g_localPauseFrame = 0;
uint32_t g_pauseControlRepeatUntil = 0;
bool g_loggedHandshake = false;
bool g_loggedPause = false;
bool g_errorShown = false;
bool g_nativePhysicalInputValid = false;
bool g_synchronizedInputValid = false;
bool g_strictStateHash = false;
uint32_t g_frameStallLogs = 0;
uint32_t g_stateHashWarnings = 0;
uint64_t g_networkWaitTotalMs = 0;
uint32_t g_networkWaitFrames = 0;
uint32_t g_networkWaitMaxMs = 0;
uint32_t g_networkSampleFrames = 0;
uint32_t g_lastTransportQueueDrops = 0;
uint32_t g_lastTransportResends = 0;

const wchar_t* ModeName(RuntimeNetworkMode mode) noexcept;
void QueueFatal(const std::wstring& message) noexcept;

void WriteAuthoritativeGlobalInput() noexcept {
    if (g_mode == RuntimeNetworkMode::kLocal || !g_synchronizedInputValid) {
        return;
    }
    ScriptInputState state{
        *reinterpret_cast<volatile uint32_t*>(GameAddresses::kRawInputMask),
        *reinterpret_cast<volatile uint32_t*>(GameAddresses::kRawInputPreviousMask),
        *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask),
        *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputPreviousMask)};
    ApplyAuthoritativeScriptInput(state, g_synchronizedInput,
                                   g_previousSynchronizedInput);
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kRawInputMask) =
        state.rawCurrent;
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kRawInputPreviousMask) =
        state.rawPrevious;
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask) =
        state.processedCurrent;
    *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputPreviousMask) =
        state.processedPrevious;
}

void __cdecl CaptureAndProjectGlobalInput() noexcept {
    const uint32_t nativeInput =
        *reinterpret_cast<volatile uint32_t*>(GameAddresses::kRawInputMask);
    g_nativePhysicalInput = nativeInput & kNetworkActionMask;
    g_nativePhysicalInputValid = true;
    WriteAuthoritativeGlobalInput();
}

__declspec(naked) int Phase13ReplayInputCallbackHook() noexcept {
    __asm {
        push eax
        call CaptureAndProjectGlobalInput
        pop eax
        mov edx, 0043B7E0h
        call edx
        // The native update may rebuild the processed mask from local devices.
        // Re-apply the authoritative state before dialogue/script consumers run.
        push eax
        call WriteAuthoritativeGlobalInput
        pop eax
        ret
    }
}

std::vector<uint8_t> RelativeJump(uintptr_t site, void* target) {
    std::vector<uint8_t> bytes(5, 0);
    bytes[0] = 0xE9;
    const intptr_t distance = reinterpret_cast<uintptr_t>(target) - (site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) {
        return {};
    }
    const int32_t displacement = static_cast<int32_t>(distance);
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return bytes;
}

void Log(const std::wstring& message) noexcept {
    if (!g_root.empty()) {
        coop::WriteLog(g_root, L"patch.log", message);
    }
}

void WriteLobbyStatus(const wchar_t* state) noexcept {
    if (g_root.empty()) {
        return;
    }
    const std::wstring path = g_root + L"\\coop\\lobby-status.ini";
    WritePrivateProfileStringW(L"lobby", L"state", state, path.c_str());
    WritePrivateProfileStringW(L"lobby", L"mode", ModeName(g_mode),
                               path.c_str());
    const std::wstring processId = std::to_wstring(GetCurrentProcessId());
    WritePrivateProfileStringW(L"lobby", L"process_id", processId.c_str(),
                               path.c_str());
    const std::wstring player1 = g_player1Airframe < 6
        ? std::to_wstring(g_player1Airframe)
        : L"-1";
    const std::wstring player2 = g_player2Airframe < 6
        ? std::to_wstring(g_player2Airframe)
        : L"-1";
    WritePrivateProfileStringW(L"lobby", L"player1", player1.c_str(),
                               path.c_str());
    WritePrivateProfileStringW(L"lobby", L"player2", player2.c_str(),
                               path.c_str());
}

bool PumpLobbyMessages() noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            g_session.Stop();
            WriteLobbyStatus(L"closed");
            Log(L"phase14 local game close requested while waiting in lobby");
            PostQuitMessage(static_cast<int>(message.wParam));
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

uint64_t LobbyResourceSignature() noexcept {
    uint64_t signature = g_runtimeBinarySignature;
    signature ^= static_cast<uint64_t>(g_player1Airframe) << 8U;
    signature ^= static_cast<uint64_t>(g_player2Airframe) << 16U;
    signature ^= 0x34505448424F4C59ULL;
    return signature;
}

std::string NarrowAscii(const wchar_t* value) {
    std::string result;
    while (*value != L'\0') {
        const wchar_t current = *value++;
        if (current > 0x7F) {
            return {};
        }
        result.push_back(static_cast<char>(current));
    }
    return result;
}

std::wstring WidenAscii(const std::string& value) {
    std::wstring result;
    for (const char current : value) {
        result.push_back(static_cast<unsigned char>(current));
    }
    return result;
}

bool ParseMode(const wchar_t* value, RuntimeNetworkMode& mode) noexcept {
    if (_wcsicmp(value, L"off") == 0 || _wcsicmp(value, L"local") == 0) {
        mode = RuntimeNetworkMode::kLocal;
        return true;
    }
    if (_wcsicmp(value, L"host") == 0) {
        mode = RuntimeNetworkMode::kHost;
        return true;
    }
    if (_wcsicmp(value, L"client") == 0) {
        mode = RuntimeNetworkMode::kClient;
        return true;
    }
    return false;
}

const wchar_t* ModeName(RuntimeNetworkMode mode) noexcept {
    switch (mode) {
    case RuntimeNetworkMode::kHost: return L"host";
    case RuntimeNetworkMode::kClient: return L"client";
    default: return L"local";
    }
}

bool ParseAirframe(const wchar_t* value, uint8_t& index) noexcept {
    for (uint8_t i = 0; i < kAirframes.size(); ++i) {
        wchar_t id[32]{};
        size_t converted = 0;
        mbstowcs_s(&converted, id, kAirframes[i].id, std::size(id) - 1);
        if (_wcsicmp(value, id) == 0) {
            index = i;
            return true;
        }
    }
    return false;
}

void QueueFatal(const std::wstring& message) noexcept {
    if (g_pendingFatal.empty()) {
        g_pendingFatal = message;
        if (g_mode != RuntimeNetworkMode::kLocal) {
            WriteLobbyStatus(L"network_error");
        }
        Log(L"phase13 network fatal: " + message);
    }
}

bool ShowFatalAndClose() noexcept {
    if (g_pendingFatal.empty()) {
        return false;
    }
    if (!g_errorShown) {
        g_errorShown = true;
        MessageBoxW(nullptr, g_pendingFatal.c_str(), L"TH12 Co-op Network Error",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        HWND window = GetActiveWindow();
        if (window != nullptr) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        PostQuitMessage(1);
    }
    return true;
}

bool PollNetwork() noexcept {
    if (!g_session.Poll(g_remoteFrames)) {
        const std::string& socketError = g_session.LastError();
        QueueFatal(socketError.empty()
                       ? L"UDP receive failed. The session cannot continue safely."
                       : WidenAscii(socketError));
        return false;
    }
    if (g_mode == RuntimeNetworkMode::kClient &&
        !g_session.LastError().empty()) {
        QueueFatal(WidenAscii(g_session.LastError()));
        return false;
    }
    if (g_session.RemoteDisconnected()) {
        QueueFatal(L"The remote player closed the session.");
        return false;
    }
    return true;
}

bool WaitForHandshake() noexcept {
    const uint64_t started = GetTickCount64();
    while (!g_session.HandshakeComplete()) {
        if (!PollNetwork()) {
            return false;
        }
        if (GetTickCount64() - started >= g_connectTimeoutMs) {
            QueueFatal(L"Room connection timed out. Check the room number, IP address, port and firewall.");
            return false;
        }
        Sleep(2);
    }
    if (!g_loggedHandshake) {
        Log(L"phase13 LAN handshake complete; lockstep input application enabled");
        g_loggedHandshake = true;
    }
    return true;
}

bool ConnectionAlive() noexcept {
    if (!PollNetwork()) {
        return false;
    }
    if (g_session.PeerSilentMilliseconds() >= g_disconnectTimeoutMs) {
        QueueFatal(L"The remote player stopped responding. Simulation was stopped before desynchronizing.");
        return false;
    }
    return true;
}

void TogglePauseIfRequested(uint32_t currentFrame) noexcept {
    // The low bit can be consumed by the native input poll before this hook
    // runs. Track the physical high-bit edge so Pause is not lost.
    const bool keyDown =
        (GetAsyncKeyState(static_cast<int>(g_pauseKey)) & 0x8000) != 0;
    const bool pressed = keyDown && !g_pauseKeyDown;
    g_pauseKeyDown = keyDown;
    if (!pressed) {
        return;
    }
    g_localPaused = !g_localPaused;
    g_localPauseFrame = g_localPaused
        ? currentFrame + static_cast<uint32_t>(g_inputDelay) + 1U
        : currentFrame;
    if (g_localPauseFrame == 0) {
        g_localPauseFrame = UINT32_MAX;
    }
    g_pauseControlRepeatUntil =
        currentFrame + static_cast<uint32_t>(g_inputRedundancy);
    g_session.SetLocalPaused(g_localPaused, g_localPauseFrame);
    Log(g_localPaused ? L"phase13 local pause requested"
                      : L"phase13 local resume requested");
}

bool WaitWhilePaused(uint32_t currentFrame) noexcept {
    TogglePauseIfRequested(currentFrame);
    if (g_localPaused || currentFrame <= g_pauseControlRepeatUntil) {
        g_session.SetLocalPaused(g_localPaused, g_localPauseFrame);
    }
    uint64_t lastControl = 0;
    auto pauseActive = [currentFrame]() noexcept {
        const bool localActive =
            g_localPaused && currentFrame >= g_localPauseFrame;
        const bool remoteActive =
            g_session.RemotePaused() &&
            currentFrame >= g_session.RemotePauseFrame();
        return localActive || remoteActive;
    };
    while (pauseActive()) {
        if (!g_loggedPause) {
            Log(L"phase13 lockstep paused at frame boundary");
            g_loggedPause = true;
        }
        if (!ConnectionAlive()) {
            return false;
        }
        TogglePauseIfRequested(currentFrame);
        const uint64_t now = GetTickCount64();
        if (now - lastControl >= 250) {
            g_session.SetLocalPaused(g_localPaused, g_localPauseFrame);
            lastControl = now;
        }
        Sleep(2);
    }
    if (g_loggedPause) {
        Log(L"phase13 lockstep resumed at frame boundary");
        g_loggedPause = false;
    }
    return true;
}

bool CompareAvailableHash(uint32_t frame) noexcept {
    const size_t index = static_cast<size_t>(
        frame % FrameInputBuffer::kCapacity);
    if (!g_hashValid[index] || g_hashFrames[index] != frame) {
        return true;
    }
    uint64_t remote = 0;
    if (!g_session.FindRemoteStateHash(frame, remote)) {
        return true;
    }
    g_hashValid[index] = false;
    if (remote != g_hashes[index]) {
        std::wstringstream message;
        message << L"State verification failed at frame " << frame
                << L". Local and remote simulations differ; local_hash=0x"
                << std::hex << g_hashes[index]
                << L"; remote_hash=0x" << remote << std::dec;
        if (g_strictStateHash) {
            QueueFatal(message.str());
            return false;
        }
        ++g_stateHashWarnings;
        message << L"; continuing in advisory mode; warning="
                << g_stateHashWarnings;
        Log(L"phase13 " + message.str());
    }
    return true;
}

void RecordNetworkPerformance(uint32_t frame, bool waited,
                              uint32_t waitMs) noexcept {
    ++g_networkSampleFrames;
    if (waited) {
        ++g_networkWaitFrames;
        g_networkWaitTotalMs += waitMs;
        g_networkWaitMaxMs = std::max(g_networkWaitMaxMs, waitMs);
    }
    constexpr uint32_t kPerformanceWindowFrames = 600;
    if (frame == 0 || frame % kPerformanceWindowFrames != 0) {
        return;
    }
    const uint32_t queueDrops = g_session.TransportQueueDrops();
    const uint32_t resends = g_session.TransportResends();
    const uint64_t averageWaitTimes100 = g_networkWaitFrames == 0
        ? 0
        : g_networkWaitTotalMs * 100ULL / g_networkWaitFrames;
    std::wstringstream diagnostic;
    diagnostic << L"phase13 network performance; frames="
               << g_networkSampleFrames
               << L"; wait_frames=" << g_networkWaitFrames
               << L"; avg_wait_ms=" << (averageWaitTimes100 / 100ULL)
               << L"." << (averageWaitTimes100 % 100ULL)
               << L"; max_wait_ms=" << g_networkWaitMaxMs
               << L"; rtt_ms=" << g_session.SmoothedRttMilliseconds()
               << L"; jitter_ms=" << g_session.JitterMilliseconds()
               << L"; recommended_delay="
               << static_cast<uint32_t>(g_session.RecommendedInputDelay())
               << L"; queue_drops="
               << (queueDrops - g_lastTransportQueueDrops)
               << L"; transport_resends="
               << (resends - g_lastTransportResends);
    Log(diagnostic.str());
    g_networkWaitTotalMs = 0;
    g_networkWaitFrames = 0;
    g_networkWaitMaxMs = 0;
    g_networkSampleFrames = 0;
    g_lastTransportQueueDrops = queueDrops;
    g_lastTransportResends = resends;
}

}  // namespace

bool InitializeRuntimeFrameInput(const std::wstring& root,
                                 std::wstring& error) noexcept {
    if (g_configured) {
        return true;
    }
    g_root = root;
    std::wstring runtimeHash;
    if (coop::Sha256File(root + L"\\th12_coop.dll", runtimeHash)) {
        g_runtimeBinarySignature = 1469598103934665603ULL;
        for (const wchar_t value : runtimeHash) {
            g_runtimeBinarySignature ^= static_cast<uint64_t>(value);
            g_runtimeBinarySignature *= 1099511628211ULL;
        }
    }
    const std::wstring configPath = root + L"\\coop\\config.ini";
    if (GetPrivateProfileIntW(L"phase6", L"enabled", 1,
                              configPath.c_str()) == 0) {
        g_configured = true;
        Log(L"phase6 frame input runtime disabled by config");
        return true;
    }

    wchar_t modeValue[16]{};
    GetPrivateProfileStringW(L"phase6", L"network_mode", L"off", modeValue,
                             static_cast<DWORD>(std::size(modeValue)),
                             configPath.c_str());
    if (!ParseMode(modeValue, g_mode)) {
        error = L"phase6.network_mode must be off, host, or client";
        return false;
    }

    const int delay = GetPrivateProfileIntW(L"phase6", L"input_delay", 2,
                                             configPath.c_str());
    const int redundancy = GetPrivateProfileIntW(
        L"phase6", L"input_redundancy", 8, configPath.c_str());
    const int connectTimeout = GetPrivateProfileIntW(
        L"phase6", L"connect_timeout_ms", 15000, configPath.c_str());
    const int disconnectTimeout = GetPrivateProfileIntW(
        L"phase6", L"disconnect_timeout_ms", 15000, configPath.c_str());
    const int inputStallTimeout = GetPrivateProfileIntW(
        L"phase6", L"input_stall_timeout_ms", 30000,
        configPath.c_str());
    const int checkInterval = GetPrivateProfileIntW(
        L"phase6", L"state_check_interval", 600, configPath.c_str());
    g_strictStateHash = GetPrivateProfileIntW(
        L"phase6", L"strict_state_hash", 0, configPath.c_str()) != 0;
    g_adaptiveDelay = GetPrivateProfileIntW(
        L"phase6", L"adaptive_delay", 0, configPath.c_str()) != 0;
    const int pauseKey = GetPrivateProfileIntW(
        L"phase6", L"pause_key", VK_PAUSE, configPath.c_str());
    if (delay < 0 || delay > 12 || redundancy < 1 || redundancy > 16 ||
        connectTimeout < 1000 || connectTimeout > 60000 ||
        disconnectTimeout < 1000 || disconnectTimeout > 60000 ||
        inputStallTimeout < 5000 || inputStallTimeout > 120000 ||
        checkInterval < 30 || checkInterval > 3600 ||
        pauseKey < 1 || pauseKey > 254) {
        error = L"phase6 lockstep delay, redundancy, timeout, hash interval or pause key is invalid";
        return false;
    }
    g_inputDelay = static_cast<uint8_t>(delay);
    g_inputRedundancy = static_cast<uint8_t>(redundancy);
    g_connectTimeoutMs = static_cast<uint32_t>(connectTimeout);
    g_disconnectTimeoutMs = static_cast<uint32_t>(disconnectTimeout);
    g_inputStallTimeoutMs = static_cast<uint32_t>(inputStallTimeout);
    g_stateCheckInterval = static_cast<uint32_t>(checkInterval);
    g_pauseKey = static_cast<uint32_t>(pauseKey);
    g_coopRules = 0;
    if (g_mode == RuntimeNetworkMode::kHost) {
        if (GetPrivateProfileIntW(L"coop_rules", L"lock_power", 0,
                                  configPath.c_str()) != 0) {
            g_coopRules |= coop::kCoopRuleLockPower;
        }
        if (GetPrivateProfileIntW(L"coop_rules", L"lock_lives", 0,
                                  configPath.c_str()) != 0) {
            g_coopRules |= coop::kCoopRuleLockLives;
        }
        if (GetPrivateProfileIntW(L"coop_rules", L"auto_bomb", 0,
                                  configPath.c_str()) != 0) {
            g_coopRules |= coop::kCoopRuleAutoBomb;
        }
        if (GetPrivateProfileIntW(L"coop_rules", L"infinite_respawn", 0,
                                  configPath.c_str()) != 0) {
            g_coopRules |= coop::kCoopRuleInfiniteRespawn;
        }
    }

    if (g_mode == RuntimeNetworkMode::kLocal) {
        wchar_t p2Value[32]{};
        GetPrivateProfileStringW(L"phase4", L"player2_airframe", L"sanae_b",
                                 p2Value,
                                 static_cast<DWORD>(std::size(p2Value)),
                                 configPath.c_str());
        if (!ParseAirframe(p2Value, g_player2Airframe)) {
            error = L"phase4 player2_airframe is invalid";
            return false;
        }
    } else {
        g_player1Airframe = 0xFF;
        g_player2Airframe = 0xFF;
    }

    if (g_mode != RuntimeNetworkMode::kLocal) {
        const std::vector<uint8_t> replacement = RelativeJump(
            GameAddresses::kReplayInputCallbackJump,
            reinterpret_cast<void*>(&Phase13ReplayInputCallbackHook));
        g_replayInputCallbackPatch = CodePatch(
            GameAddresses::kReplayInputCallbackJump,
            {0xE9, 0xC9, 0xF2, 0xFF, 0xFF}, replacement);
        if (replacement.size() != 5 || !g_replayInputCallbackPatch.Install()) {
            error = L"unable to install synchronized global input callback";
            return false;
        }
        Log(L"phase13 host-authoritative global script input hook installed");
        wchar_t peerAddress[64]{};
        GetPrivateProfileStringW(L"phase6", L"peer_address", L"127.0.0.1",
                                 peerAddress,
                                 static_cast<DWORD>(std::size(peerAddress)),
                                 configPath.c_str());
        const int listenPort = GetPrivateProfileIntW(
            L"phase6", L"listen_port",
            g_mode == RuntimeNetworkMode::kHost ? 28765 : 0,
            configPath.c_str());
        const int peerPort = GetPrivateProfileIntW(
            L"phase6", L"peer_port", 28765, configPath.c_str());
        const int sessionId = GetPrivateProfileIntW(
            L"phase6", L"session_id", 12012, configPath.c_str());
        if (listenPort < 0 || listenPort > 65535 ||
            (g_mode == RuntimeNetworkMode::kClient &&
             (peerPort < 1 || peerPort > 65535)) ||
            sessionId <= 0) {
            error = L"phase6 UDP ports or session_id are invalid";
            return false;
        }
        LanSessionConfig config{};
        config.role = g_mode == RuntimeNetworkMode::kHost
                          ? LanSessionRole::kHost
                          : LanSessionRole::kClient;
        config.sessionId = static_cast<uint32_t>(sessionId);
        config.listenPort = static_cast<uint16_t>(listenPort);
        config.peerAddress = NarrowAscii(peerAddress);
        config.peerPort = static_cast<uint16_t>(peerPort);
        config.localPlayer = g_mode == RuntimeNetworkMode::kHost ? 1 : 2;
        config.inputDelay = g_inputDelay;
        config.inputRedundancy = g_inputRedundancy;
        config.disconnectTimeoutMs = g_disconnectTimeoutMs;
        std::string networkError;
        if (!g_session.Start(config, networkError)) {
            error = L"phase13 LAN session failed: " + WidenAscii(networkError);
            return false;
        }
        g_localSlot = g_mode == RuntimeNetworkMode::kHost
            ? LogicalPlayerSlot::kPlayer1
            : LogicalPlayerSlot::kPlayer2;
        if (!g_lockstep.Configure(g_localSlot, g_inputDelay)) {
            error = L"unable to configure lockstep timeline";
            return false;
        }
    }

    ResetRuntimeFrameInputTimeline();
    g_ready = true;
    g_configured = true;
    WriteLobbyStatus(g_mode == RuntimeNetworkMode::kLocal
                         ? L"local"
                         : L"starting");

    std::wstringstream message;
    message << L"phase13 frame input runtime armed; mode=" << ModeName(g_mode)
            << L"; delay=" << static_cast<uint32_t>(g_inputDelay)
            << L"; redundancy=" << static_cast<uint32_t>(g_inputRedundancy)
            << L"; disconnect_timeout_ms=" << g_disconnectTimeoutMs
            << L"; input_stall_timeout_ms=" << g_inputStallTimeoutMs
            << L"; state_interval=" << g_stateCheckInterval
            << L"; strict_state_hash=" << (g_strictStateHash ? 1 : 0)
            << L"; adaptive_delay=" << (g_adaptiveDelay ? 1 : 0)
            << L"; network_apply="
            << (g_mode == RuntimeNetworkMode::kLocal ? 0 : 1)
            << L"; protocol=" << kNetworkProtocolVersion
            << L"; input_transport=bundle32";
    if (g_session.IsRunning()) {
        message << L"; udp_port=" << g_session.LocalPort();
    }
    Log(message.str());
    return true;
}

bool CoordinateRuntimeAirframes(uint32_t localSelectedAirframe,
                                uint32_t& player1Airframe,
                                uint32_t& player2Airframe) noexcept {
    if (!g_ready || localSelectedAirframe >= 6) {
        QueueFatal(L"The local player-select result is invalid.");
        ShowFatalAndClose();
        return false;
    }
    if (g_mode == RuntimeNetworkMode::kLocal) {
        g_player1Airframe = static_cast<uint8_t>(localSelectedAirframe);
        player1Airframe = g_player1Airframe;
        player2Airframe = g_player2Airframe;
        g_lobbySelectionsReady = true;
        g_lobbyStarted = true;
        return g_player2Airframe < 6;
    }

    g_session.ResetLobby();
    g_player1Airframe = g_mode == RuntimeNetworkMode::kHost
        ? static_cast<uint8_t>(localSelectedAirframe)
        : uint8_t{0xFF};
    g_player2Airframe = g_mode == RuntimeNetworkMode::kClient
        ? static_cast<uint8_t>(localSelectedAirframe)
        : uint8_t{0xFF};
    g_lobbySelectionsReady = false;
    g_lobbyResourcesReady = false;
    g_lobbyStarted = false;
    g_lobbyGameRng = {};
    g_lobbyGameRngReady = false;
    WriteLobbyStatus(L"waiting_peer");

    const uint64_t handshakeStarted = GetTickCount64();
    uint64_t lastHandshakeLog = 0;
    while (!g_session.HandshakeComplete()) {
        if (!PollNetwork() || !PumpLobbyMessages()) {
            ShowFatalAndClose();
            return false;
        }
        const uint64_t now = GetTickCount64();
        if (now - lastHandshakeLog >= 2000) {
            std::wstringstream diagnostic;
            diagnostic << L"phase13 waiting for LAN handshake; role="
                       << ModeName(g_mode)
                       << L"; local_port=" << g_session.LocalPort()
                       << L"; hello_attempts=" << g_session.HelloAttempts()
                       << L"; hello_received="
                       << g_session.HelloPacketsReceived()
                       << L"; ack_sent=" << g_session.HelloAcksSent()
                       << L"; ack_received=" << g_session.HelloAcksReceived();
            Log(diagnostic.str());
            lastHandshakeLog = now;
        }
        if (g_mode == RuntimeNetworkMode::kClient &&
            now - handshakeStarted >= g_connectTimeoutMs) {
            std::wstringstream failure;
            failure << L"Unable to find the host after "
                    << g_session.HelloAttempts()
                    << L" UDP hello attempts. Direct IPv4 and same-LAN broadcast both failed. Check the room number, UDP port, host firewall, and Wi-Fi client isolation.";
            QueueFatal(failure.str());
            ShowFatalAndClose();
            return false;
        }
        Sleep(2);
    }
    if (!g_loggedHandshake) {
        std::wstringstream connected;
        connected << L"phase13 LAN handshake complete; lobby selection enabled"
                  << L"; peer_source="
                  << (g_session.PeerWasDiscovered() ? L"discovery" : L"direct")
                  << L"; hello_attempts=" << g_session.HelloAttempts()
                  << L"; hello_received="
                  << g_session.HelloPacketsReceived()
                  << L"; ack_sent=" << g_session.HelloAcksSent()
                  << L"; ack_received=" << g_session.HelloAcksReceived();
        Log(connected.str());
        g_loggedHandshake = true;
    }

    uint64_t lastSelectionSend = 0;
    for (;;) {
        const uint64_t now = GetTickCount64();
        if (now - lastSelectionSend >= 100) {
            g_session.SendLobbySelection(g_player1Airframe,
                                         g_player2Airframe);
            lastSelectionSend = now;
        }
        if (!PollNetwork() || !PumpLobbyMessages()) {
            ShowFatalAndClose();
            return false;
        }
        uint8_t remotePlayer1 = 0xFF;
        uint8_t remotePlayer2 = 0xFF;
        if (g_session.GetRemoteLobbySelection(remotePlayer1, remotePlayer2)) {
            if (g_mode == RuntimeNetworkMode::kHost &&
                remotePlayer1 == 0xFF && remotePlayer2 < 6) {
                g_player2Airframe = remotePlayer2;
                break;
            }
            if (g_mode == RuntimeNetworkMode::kClient &&
                remotePlayer1 < 6 && remotePlayer2 == 0xFF) {
                g_player1Airframe = remotePlayer1;
                break;
            }
        }
        // Ready contains the complete agreed pair. Treat it as an implicit
        // selection acknowledgement so one lost final selection datagram
        // cannot leave the slower peer in this loop forever.
        uint8_t readyPlayer1 = 0xFF;
        uint8_t readyPlayer2 = 0xFF;
        uint64_t readySignature = 0;
        uint8_t readyDelay = 1;
        if (g_session.RemoteLobbyReady(readyPlayer1, readyPlayer2,
                                       readySignature, readyDelay)) {
            if (g_mode == RuntimeNetworkMode::kHost &&
                readyPlayer1 == g_player1Airframe && readyPlayer2 < 6) {
                g_player2Airframe = readyPlayer2;
                Log(L"phase14 lobby selection recovered from peer Ready");
                break;
            }
            if (g_mode == RuntimeNetworkMode::kClient &&
                readyPlayer1 < 6 && readyPlayer2 == g_player2Airframe) {
                g_player1Airframe = readyPlayer1;
                Log(L"phase14 lobby selection recovered from peer Ready");
                break;
            }
        }
        Sleep(2);
    }

    player1Airframe = g_player1Airframe;
    player2Airframe = g_player2Airframe;
    g_lobbySelectionsReady = player1Airframe < 6 && player2Airframe < 6;
    if (!g_lobbySelectionsReady) {
        QueueFatal(L"The room did not produce a valid P1/P2 airframe pair.");
        ShowFatalAndClose();
        return false;
    }
    WriteLobbyStatus(L"loading_resources");
    std::wstringstream message;
    message << L"phase14 lobby airframes agreed; p1=" << player1Airframe
            << L"; p2=" << player2Airframe;
    Log(message.str());
    return true;
}

void MarkRuntimeLobbyResourcesReady(bool ready) noexcept {
    if (g_mode == RuntimeNetworkMode::kLocal) {
        return;
    }
    g_lobbyResourcesReady = ready;
    if (!ready) {
        WriteLobbyStatus(L"resource_error");
        QueueFatal(L"Player resources failed to initialize on this computer.");
        return;
    }
    WriteLobbyStatus(L"ready");
    Log(L"phase14 local player resources ready");
}

bool WaitForLobbyStart() noexcept {
    if (g_mode == RuntimeNetworkMode::kLocal || g_lobbyStarted) {
        return true;
    }
    if (!g_lobbySelectionsReady || !g_lobbyResourcesReady) {
        QueueFatal(L"The room reached frame one before player resources were ready.");
        return false;
    }

    const uint64_t resourceSignature = LobbyResourceSignature();
    WriteLobbyStatus(L"waiting_start");
    uint64_t lastReadySend = 0;
    const uint64_t probeDeadline = g_adaptiveDelay
        ? GetTickCount64() + 750U
        : GetTickCount64();
    for (;;) {
        const uint64_t now = GetTickCount64();
        if (now - lastReadySend >= 100) {
            // Keep selection packets alive until the start barrier releases.
            // The peer may still be in its selection loop after UDP loss.
            g_session.SendLobbySelection(g_player1Airframe,
                                         g_player2Airframe);
            g_session.SendLobbyReady(g_player1Airframe, g_player2Airframe,
                                     resourceSignature,
                                     g_adaptiveDelay
                                         ? g_session.RecommendedInputDelay()
                                         : g_inputDelay);
            lastReadySend = now;
        }
        if (!PollNetwork() || !PumpLobbyMessages()) {
            return false;
        }

        uint8_t remotePlayer1 = 0xFF;
        uint8_t remotePlayer2 = 0xFF;
        uint64_t remoteSignature = 0;
        uint8_t remoteRecommendedDelay = 1;
        if (g_session.RemoteLobbyReady(remotePlayer1, remotePlayer2,
                                       remoteSignature,
                                       remoteRecommendedDelay)) {
            if (remotePlayer1 != g_player1Airframe ||
                remotePlayer2 != g_player2Airframe ||
                remoteSignature != resourceSignature) {
                WriteLobbyStatus(L"configuration_mismatch");
                QueueFatal(L"Room airframes or patch resources do not match.");
                return false;
            }
            if (g_mode == RuntimeNetworkMode::kHost) {
                if (g_adaptiveDelay && now < probeDeadline) {
                    Sleep(2);
                    continue;
                }
                const uint8_t negotiatedDelay = g_adaptiveDelay
                    ? (std::max)(g_inputDelay, remoteRecommendedDelay)
                    : g_inputDelay;
                if (!g_lobbyGameRngReady) {
                    g_lobbyGameRng = ReadRuntimeGameRngState();
                    g_lobbyGameRngReady = true;
                    std::wstringstream rngMessage;
                    rngMessage << L"phase13 host captured TH12 RNG start state; primary="
                               << g_lobbyGameRng.primary.seed << L"/"
                               << g_lobbyGameRng.primary.calls
                               << L"; secondary="
                               << g_lobbyGameRng.secondary.seed << L"/"
                               << g_lobbyGameRng.secondary.calls;
                    Log(rngMessage.str());
                }
                for (int repeat = 0; repeat < 8; ++repeat) {
                    g_session.SendLobbyStart(g_timeline, g_lobbyGameRng,
                                             negotiatedDelay, g_coopRules);
                    Sleep(1);
                }
                if (!g_lockstep.Configure(g_localSlot, negotiatedDelay)) {
                    QueueFatal(L"Unable to apply the negotiated input delay.");
                    return false;
                }
                g_inputDelay = negotiatedDelay;
                std::wstringstream delayMessage;
                if (g_adaptiveDelay) {
                    delayMessage << L"phase13 host negotiated input delay="
                                 << static_cast<uint32_t>(negotiatedDelay)
                                 << L"; peer_recommended="
                                 << static_cast<uint32_t>(remoteRecommendedDelay)
                                 << L"; rtt_ms="
                                 << g_session.SmoothedRttMilliseconds()
                                 << L"; jitter_ms="
                                 << g_session.JitterMilliseconds();
                } else {
                    delayMessage << L"phase13 fixed input delay applied="
                                 << static_cast<uint32_t>(negotiatedDelay);
                }
                Log(delayMessage.str());
                WriteRuntimeGameRngState(g_lobbyGameRng);
                g_lobbyStarted = true;
                break;
            }
        }
        if (g_mode == RuntimeNetworkMode::kClient) {
            uint32_t startTimeline = 0;
            GameRngState startGameRng{};
            uint8_t negotiatedDelay = 1;
            uint8_t negotiatedRules = 0;
            if (g_session.LobbyStartReceived(startTimeline, startGameRng,
                                             negotiatedDelay,
                                             negotiatedRules)) {
                if (startTimeline != g_timeline) {
                    QueueFatal(L"Room start timeline does not match locally.");
                    return false;
                }
                if (negotiatedDelay < g_inputDelay || negotiatedDelay > 12 ||
                    !g_lockstep.Configure(g_localSlot, negotiatedDelay)) {
                    QueueFatal(L"Host supplied an invalid negotiated input delay.");
                    return false;
                }
                g_inputDelay = negotiatedDelay;
                if ((negotiatedRules & ~coop::kCoopRuleMask) != 0) {
                    QueueFatal(L"Host supplied invalid co-op gameplay rules.");
                    return false;
                }
                g_coopRules = negotiatedRules;
                WriteRuntimeGameRngState(startGameRng);
                if (ReadRuntimeGameRngState() != startGameRng) {
                    QueueFatal(L"Unable to apply the host TH12 RNG start state.");
                    return false;
                }
                g_lobbyGameRng = startGameRng;
                g_lobbyGameRngReady = true;
                std::wstringstream rngMessage;
                rngMessage << L"phase13 client applied TH12 RNG start state; primary="
                           << g_lobbyGameRng.primary.seed << L"/"
                           << g_lobbyGameRng.primary.calls
                           << L"; secondary="
                           << g_lobbyGameRng.secondary.seed << L"/"
                           << g_lobbyGameRng.secondary.calls;
                Log(rngMessage.str());
                std::wstringstream delayMessage;
                if (g_adaptiveDelay) {
                    delayMessage << L"phase13 client applied input delay="
                                 << static_cast<uint32_t>(negotiatedDelay)
                                 << L"; measured_rtt_ms="
                                 << g_session.SmoothedRttMilliseconds()
                                 << L"; jitter_ms="
                                 << g_session.JitterMilliseconds();
                } else {
                    delayMessage << L"phase13 fixed input delay applied="
                                 << static_cast<uint32_t>(negotiatedDelay);
                }
                Log(delayMessage.str());
                g_lobbyStarted = true;
                break;
            }
        }
        Sleep(2);
    }
    WriteLobbyStatus(L"running");
    std::wstringstream ruleMessage;
    ruleMessage << L"phase14 room start barrier released; coop_rules="
                << static_cast<uint32_t>(g_coopRules)
                << L"; lock_power="
                << (coop::CoopRuleEnabled(g_coopRules,
                                          coop::kCoopRuleLockPower) ? 1 : 0)
                << L"; lock_lives="
                << (coop::CoopRuleEnabled(g_coopRules,
                                          coop::kCoopRuleLockLives) ? 1 : 0)
                << L"; auto_bomb="
                << (coop::CoopRuleEnabled(g_coopRules,
                                          coop::kCoopRuleAutoBomb) ? 1 : 0)
                << L"; infinite_respawn="
                << (coop::CoopRuleEnabled(
                        g_coopRules, coop::kCoopRuleInfiniteRespawn) ? 1 : 0);
    Log(ruleMessage.str());
    return true;
}

void ResetRuntimeFrameInputTimeline() noexcept {
    g_clock.Reset();
    g_lockstep.Reset();
    g_localFrames.Clear();
    g_remoteFrames.Clear();
    g_frameLatch.Clear();
    g_hashFrames.fill(0);
    g_hashes.fill(0);
    g_hashValid.fill(false);
    g_pendingFatal.clear();
    g_lastHashFrame = 0;
    g_lastHash = 0;
    g_localPaused = false;
    g_pauseKeyDown = false;
    g_localPauseFrame = 0;
    g_pauseControlRepeatUntil = 0;
    g_loggedPause = false;
    g_errorShown = false;
    g_frameStallLogs = 0;
    g_stateHashWarnings = 0;
    g_networkWaitTotalMs = 0;
    g_networkWaitFrames = 0;
    g_networkWaitMaxMs = 0;
    g_networkSampleFrames = 0;
    g_lastTransportQueueDrops = g_session.TransportQueueDrops();
    g_lastTransportResends = g_session.TransportResends();
    g_nativePhysicalInput = 0;
    g_synchronizedInput = 0;
    g_previousSynchronizedInput = 0;
    g_nativePhysicalInputValid = false;
    g_synchronizedInputValid = false;
    ++g_timeline;
    if (g_timeline == 0) {
        g_timeline = 1;
    }
    if (g_session.IsRunning()) {
        g_session.ResetTimeline(g_timeline);
    }
    if (g_session.HandshakeComplete()) {
        g_session.SetLocalPaused(false, 1);
    }
    if (g_configured || g_ready) {
        std::wstringstream message;
        message << L"phase13 frame input timeline reset; timeline="
                << g_timeline << L"; first_frame=1";
        Log(message.str());
    }
}

bool BeginRuntimeFrameInput(uint32_t player1Mask, uint32_t player2Mask,
                            FrameInput& input) noexcept {
    if (!g_ready || g_frameLatch.HasPending()) {
        return false;
    }
    if (g_mode == RuntimeNetworkMode::kLocal) {
        input = {g_clock.Next(), player1Mask & 0xFFU, player2Mask & 0xFFU};
        return g_frameLatch.Publish(input);
    }
    if (ShowFatalAndClose() || !WaitForHandshake() ||
        !WaitForLobbyStart() ||
        !WaitWhilePaused(g_lockstep.SimulationFrame())) {
        ShowFatalAndClose();
        return false;
    }

    const uint32_t localPhysicalMask = player1Mask & 0xFFU;
    FrameInput capture{};
    if (!g_lockstep.BuildLocalCapture(localPhysicalMask, capture)) {
        QueueFatal(L"Unable to schedule local input on the lockstep timeline.");
        ShowFatalAndClose();
        return false;
    }
    FrameInput existing{};
    if (!g_localFrames.Find(capture.frame, existing)) {
        g_localFrames.Push(capture);
    }
    g_session.SendInput(capture);

    FrameInput warmLocal{};
    FrameInput warmRemote{};
    if (g_lockstep.BuildWarmupInputs(warmLocal, warmRemote)) {
        g_localFrames.Push(warmLocal);
        g_remoteFrames.Push(warmRemote);
    }

    const uint32_t frame = g_lockstep.SimulationFrame();
    FrameInput local{};
    FrameInput remote{};
    const uint64_t frameWaitStarted = GetTickCount64();
    uint64_t lastRecoverySend = frameWaitStarted;
    uint32_t recoverySends = 0;
    bool waitedForRemote = false;
    bool stallLogged = false;
    while (!g_localFrames.Find(frame, local) ||
           !g_remoteFrames.Find(frame, remote)) {
        waitedForRemote = true;
        if (!ConnectionAlive()) {
            ShowFatalAndClose();
            return false;
        }
        const uint64_t now = GetTickCount64();
        const uint64_t waited = now - frameWaitStarted;
        // Normally the transport thread repairs loss by resending its latest
        // 32-frame bundle. During a mutual lockstep stall both simulations
        // stop publishing new frames; send the captured bundle directly as a
        // second recovery path so one missing frame cannot deadlock both ends.
        if (waited >= 33 && now - lastRecoverySend >= 33) {
            if (g_session.SendInput(capture)) {
                ++recoverySends;
            }
            lastRecoverySend = now;
        }
        if (!stallLogged && waited >= 250 && g_frameStallLogs < 8) {
            std::wstringstream diagnostic;
            diagnostic << L"phase13 input stall; waiting_frame=" << frame
                       << L"; capture_frame=" << capture.frame
                       << L"; last_remote_frame="
                       << g_session.LastReceivedFrame()
                       << L"; timeline=" << g_timeline
                       << L"; remote_timeline="
                       << g_session.LastObservedInputTimeline()
                       << L"; peer_silent_ms="
                       << g_session.PeerSilentMilliseconds();
            Log(diagnostic.str());
            stallLogged = true;
            ++g_frameStallLogs;
        }
        if (waited >= g_inputStallTimeoutMs) {
            std::wstringstream failure;
            failure << L"Timed out waiting for remote input frame " << frame
                    << L"; last_remote_frame="
                    << g_session.LastReceivedFrame()
                    << L"; local_timeline=" << g_timeline
                    << L"; remote_timeline="
                    << g_session.LastObservedInputTimeline()
                    << L"; wrong_timeline_packets="
                    << g_session.WrongTimelineInputPackets()
                    << L"; peer_silent_ms="
                    << g_session.PeerSilentMilliseconds()
                    << L"; waited_ms=" << waited
                    << L"; rtt_ms=" << g_session.SmoothedRttMilliseconds()
                    << L"; jitter_ms=" << g_session.JitterMilliseconds()
                    << L"; queue_drops=" << g_session.TransportQueueDrops()
                    << L"; transport_resends="
                    << g_session.TransportResends() << L".";
            QueueFatal(failure.str());
            ShowFatalAndClose();
            return false;
        }
        // The transport signals this event as soon as a UDP packet is queued.
        // The short timeout still services pause and disconnect state.
        g_session.WaitForIncoming(4);
    }
    const uint32_t frameWaitMs = static_cast<uint32_t>(std::min<uint64_t>(
        GetTickCount64() - frameWaitStarted, UINT32_MAX));
    RecordNetworkPerformance(frame, waitedForRemote, frameWaitMs);
    if (stallLogged) {
        std::wstringstream recovered;
        recovered << L"phase13 input stall recovered; frame=" << frame
                  << L"; waited_ms=" << frameWaitMs
                  << L"; direct_recovery_sends=" << recoverySends
                  << L"; transport_resends="
                  << g_session.TransportResends();
        Log(recovered.str());
    }
    if (!CompareAvailableHash(g_lastHashFrame) ||
        !g_lockstep.Resolve(local, remote, input)) {
        if (g_pendingFatal.empty()) {
            QueueFatal(L"Unable to merge host and client input for the current frame.");
        }
        ShowFatalAndClose();
        return false;
    }
    g_previousSynchronizedInput = g_synchronizedInput;
    g_synchronizedInput = input.player1Mask & 0xFFU;
    g_synchronizedInputValid = true;
    const uint32_t clockFrame = g_clock.Next();
    if (clockFrame != input.frame || !g_frameLatch.Publish(input)) {
        QueueFatal(L"Lockstep frame numbering diverged locally.");
        ShowFatalAndClose();
        return false;
    }
    return true;
}

bool ConsumeRuntimeFrameInput(FrameInput& input) noexcept {
    return g_ready && g_frameLatch.Consume(input);
}

bool CaptureRuntimeFrameInput(uint32_t player1Mask, uint32_t player2Mask,
                              FrameInput& input) noexcept {
    return BeginRuntimeFrameInput(player1Mask, player2Mask, input);
}

void SubmitRuntimeStateHash(uint32_t frame, uint64_t stateHash) noexcept {
    if (!RuntimeNetworkActive() || frame == 0 ||
        frame % g_stateCheckInterval != 0) {
        return;
    }
    const size_t index = static_cast<size_t>(
        frame % FrameInputBuffer::kCapacity);
    g_hashFrames[index] = frame;
    g_hashes[index] = stateHash;
    g_hashValid[index] = true;
    g_lastHashFrame = frame;
    g_lastHash = stateHash;
    DeterminismSample sample{};
    if (RuntimeLastDeterminismSample(sample)) {
        std::wstringstream diagnostic;
        diagnostic << L"phase13 state sample; frame=" << sample.frame
                   << L"; input=" << sample.player1Input << L"/"
                   << sample.player2Input
                   << L"; p1=(" << sample.player1X << L"," << sample.player1Y
                   << L") focus=" << sample.player1Focus
                   << L"; p2=(" << sample.player2X << L"," << sample.player2Y
                   << L") focus=" << sample.player2Focus
                   << L"; state=" << sample.player1State << L"/"
                   << sample.player2State
                   << L"; state_frame=" << sample.player1StateFrame << L"/"
                   << sample.player2StateFrame
                   << L"; inv=" << sample.player1Invincibility << L"/"
                   << sample.player2Invincibility
                   << L"; recovery=" << sample.player2RecoveryPhase
                   << L"; resources=" << sample.power << L"/"
                   << sample.lives << L"/" << sample.bombs
                   << L"/" << sample.lifeFragments << L"/"
                   << sample.bombFragments
                   << L"; rng=" << sample.gameRng.primary.seed << L"/"
                   << sample.gameRng.primary.calls << L"," 
                   << sample.gameRng.secondary.seed << L"/"
                   << sample.gameRng.secondary.calls
                   << L"; combat=" << sample.player1ActiveBullets << L"/"
                   << sample.player2ActiveBullets << L"," 
                   << sample.player1ActiveOptions << L"/"
                   << sample.player2ActiveOptions << L"," 
                   << sample.player1ActiveDamageAreas << L"/"
                   << sample.player2ActiveDamageAreas
                   << L"; bomb_state=" << sample.player1BombState << L"/"
                   << sample.player2BombState
                   << L"; collision_hash=0x" << std::hex
                   << sample.collisionHash
                   << L"; hash=0x" << std::hex << sample.stateHash << std::dec;
        Log(diagnostic.str());
    }
    g_session.SendStateHash(frame, stateHash);
    CompareAvailableHash(frame);
}

bool RuntimeNetworkActive() noexcept {
    return g_ready && g_mode != RuntimeNetworkMode::kLocal;
}

bool RuntimeStateHashDue(uint32_t frame) noexcept {
    return RuntimeNetworkActive() && frame != 0 &&
           frame % g_stateCheckInterval == 0;
}

uint32_t RuntimeFrameNumber() noexcept {
    return g_ready ? g_clock.Current() : 0;
}

uint32_t RuntimePhysicalPlayer1Input() noexcept {
    if (RuntimeNetworkActive() && g_nativePhysicalInputValid) {
        g_nativePhysicalInputValid = false;
        return g_nativePhysicalInput;
    }
    return *reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask) &
           0xFFU;
}

LogicalPlayerSlot RuntimeLocalPlayerSlot() noexcept {
    return g_localSlot;
}

uint8_t RuntimeCoopRules() noexcept {
    return g_coopRules;
}

}  // namespace coop::th12
