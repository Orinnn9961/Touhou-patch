#include "lan_session.h"

#include "network_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <array>
#include <algorithm>
#include <new>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace coop::th12 {
namespace {

SOCKET NativeSocket(uintptr_t handle) noexcept {
    return static_cast<SOCKET>(handle);
}

uint8_t RoleFlag(LanSessionRole role) noexcept {
    return static_cast<uint8_t>(role);
}

uint8_t PlayerForRole(LanSessionRole role) noexcept {
    return role == LanSessionRole::kHost ? uint8_t{1} : uint8_t{2};
}

LanSessionRole RemoteRole(LanSessionRole role) noexcept {
    return role == LanSessionRole::kHost ? LanSessionRole::kClient
                                         : LanSessionRole::kHost;
}

uint64_t TickNow() noexcept {
    return GetTickCount64();
}

uint32_t ConfigWord(const LanSessionConfig& config) noexcept {
    return PackNetworkConfig(config.inputDelay, config.inputRedundancy);
}

bool IsTransientUdpError(int error) noexcept {
    switch (error) {
    case WSAEWOULDBLOCK:
    case WSAECONNRESET:
    case WSAENETRESET:
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
        return true;
    default:
        return false;
    }
}

std::string SocketError(const char* operation, int error) {
    return std::string(operation) + "; WinSock error=" +
           std::to_string(error);
}

}  // namespace

struct LanInputSession::AsyncTransport {
    struct Datagram {
        std::array<uint8_t, kNetworkPacketSize> bytes{};
        int size{};
        uint32_t sourceAddress{};
        uint16_t sourcePort{};
    };

    static constexpr LONG kQueueCapacity = 512;

    AsyncTransport() noexcept {
        InitializeCriticalSection(&resendLock);
    }

    ~AsyncTransport() {
        Shutdown();
        DeleteCriticalSection(&resendLock);
    }

    bool Start(SOCKET socketHandle) noexcept {
        socket = socketHandle;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        incomingEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        socketEvent = WSACreateEvent();
        if (stopEvent == nullptr || incomingEvent == nullptr ||
            socketEvent == WSA_INVALID_EVENT ||
            WSAEventSelect(socket, socketEvent, FD_READ) == SOCKET_ERROR) {
            Shutdown();
            return false;
        }
        thread = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
        if (thread == nullptr) {
            Shutdown();
            return false;
        }
        // The receive thread normally sleeps in the kernel. A small priority
        // boost lets it empty the UDP queue promptly after Wi-Fi/VPN bursts.
        SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
        return true;
    }

    void Shutdown() noexcept {
        if (stopEvent != nullptr) {
            SetEvent(stopEvent);
        }
        if (thread != nullptr) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
            thread = nullptr;
        }
        if (socket != INVALID_SOCKET && socketEvent != WSA_INVALID_EVENT) {
            WSAEventSelect(socket, nullptr, 0);
        }
        if (socketEvent != WSA_INVALID_EVENT) {
            WSACloseEvent(socketEvent);
            socketEvent = WSA_INVALID_EVENT;
        }
        if (incomingEvent != nullptr) {
            CloseHandle(incomingEvent);
            incomingEvent = nullptr;
        }
        if (stopEvent != nullptr) {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
        socket = INVALID_SOCKET;
        InterlockedExchange(&readIndex, 0);
        InterlockedExchange(&writeIndex, 0);
        InterlockedExchange(&receiveError, 0);
        EnterCriticalSection(&resendLock);
        resendValid = false;
        LeaveCriticalSection(&resendLock);
    }

    bool Pop(Datagram& datagram) noexcept {
        const LONG read = InterlockedCompareExchange(&readIndex, 0, 0);
        const LONG write = InterlockedCompareExchange(&writeIndex, 0, 0);
        if (read == write) {
            return false;
        }
        datagram = queue[static_cast<size_t>(read)];
        InterlockedExchange(&readIndex, (read + 1) % kQueueCapacity);
        return true;
    }

    void PublishResend(
        const std::array<uint8_t, kNetworkPacketSize>& bytes,
        uint32_t address, uint16_t port) noexcept {
        EnterCriticalSection(&resendLock);
        resendBytes = bytes;
        resendAddress = address;
        resendPort = port;
        resendValid = true;
        resendPublishedTick = TickNow();
        LeaveCriticalSection(&resendLock);
    }

    static DWORD WINAPI ThreadMain(void* argument) noexcept {
        auto* transport = static_cast<AsyncTransport*>(argument);
        transport->Run();
        return 0;
    }

    void Run() noexcept {
        HANDLE events[2]{stopEvent, socketEvent};
        uint64_t lastResendTick = TickNow();
        for (;;) {
            const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 8);
            if (wait == WAIT_OBJECT_0) {
                return;
            }
            if (wait == WAIT_OBJECT_0 + 1) {
                WSANETWORKEVENTS networkEvents{};
                WSAEnumNetworkEvents(socket, socketEvent, &networkEvents);
                DrainSocket();
            }

            const uint64_t now = TickNow();
            if (now - lastResendTick < 33) {
                continue;
            }
            std::array<uint8_t, kNetworkPacketSize> bytes{};
            uint32_t address = 0;
            uint16_t port = 0;
            uint64_t publishedTick = 0;
            bool valid = false;
            EnterCriticalSection(&resendLock);
            if (resendValid) {
                bytes = resendBytes;
                address = resendAddress;
                port = resendPort;
                publishedTick = resendPublishedTick;
                valid = true;
            }
            LeaveCriticalSection(&resendLock);
            if (!valid) {
                lastResendTick = now;
                continue;
            }

            // A newly published input has already been sent by the game
            // thread. Only retransmit if no newer frame arrived for 33 ms.
            if (now - publishedTick < 33) {
                continue;
            }
            sockaddr_in destination{};
            destination.sin_family = AF_INET;
            destination.sin_addr.s_addr = address;
            destination.sin_port = htons(port);
            const int sent = sendto(
                socket, reinterpret_cast<const char*>(bytes.data()),
                static_cast<int>(bytes.size()), 0,
                reinterpret_cast<const sockaddr*>(&destination),
                sizeof(destination));
            if (sent == static_cast<int>(bytes.size())) {
                InterlockedIncrement(&resends);
            }
            lastResendTick = now;
        }
    }

    void DrainSocket() noexcept {
        for (;;) {
            Datagram datagram{};
            sockaddr_in source{};
            int sourceSize = sizeof(source);
            const int result = recvfrom(
                socket, reinterpret_cast<char*>(datagram.bytes.data()),
                static_cast<int>(datagram.bytes.size()), 0,
                reinterpret_cast<sockaddr*>(&source), &sourceSize);
            if (result == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) {
                    return;
                }
                if (IsTransientUdpError(error) || error == WSAEMSGSIZE) {
                    continue;
                }
                InterlockedCompareExchange(&receiveError, error, 0);
                SetEvent(incomingEvent);
                return;
            }
            datagram.size = result;
            datagram.sourceAddress = source.sin_addr.s_addr;
            datagram.sourcePort = ntohs(source.sin_port);

            const LONG write = InterlockedCompareExchange(&writeIndex, 0, 0);
            const LONG read = InterlockedCompareExchange(&readIndex, 0, 0);
            const LONG next = (write + 1) % kQueueCapacity;
            if (next == read) {
                InterlockedIncrement(&queueDrops);
                continue;
            }
            queue[static_cast<size_t>(write)] = datagram;
            InterlockedExchange(&writeIndex, next);
            SetEvent(incomingEvent);
        }
    }

    SOCKET socket{INVALID_SOCKET};
    HANDLE stopEvent{};
    HANDLE incomingEvent{};
    WSAEVENT socketEvent{WSA_INVALID_EVENT};
    HANDLE thread{};
    std::array<Datagram, static_cast<size_t>(kQueueCapacity)> queue{};
    volatile LONG readIndex{};
    volatile LONG writeIndex{};
    volatile LONG receiveError{};
    volatile LONG queueDrops{};
    volatile LONG resends{};
    CRITICAL_SECTION resendLock{};
    std::array<uint8_t, kNetworkPacketSize> resendBytes{};
    uint32_t resendAddress{};
    uint16_t resendPort{};
    uint64_t resendPublishedTick{};
    bool resendValid{};
};

bool LanSessionConfig::IsValid() const noexcept {
    return (role == LanSessionRole::kHost || role == LanSessionRole::kClient) &&
           sessionId != 0 && localPlayer == PlayerForRole(role) &&
           inputDelay <= 12 && inputRedundancy >= 1 && inputRedundancy <= 16 &&
           disconnectTimeoutMs >= 1000 && disconnectTimeoutMs <= 60000 &&
           (role == LanSessionRole::kHost ||
            (!peerAddress.empty() && peerPort != 0));
}

LanInputSession::~LanInputSession() {
    Stop();
}

bool LanInputSession::Start(const LanSessionConfig& config,
                            std::string& error) noexcept {
    Stop();
    if (!config.IsValid()) {
        error = "invalid LAN session configuration";
        return false;
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "WSAStartup failed";
        return false;
    }
    winsockStarted_ = true;

    const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) {
        error = "unable to create UDP socket";
        Stop();
        return false;
    }
    socket_ = static_cast<uintptr_t>(socketHandle);

    BOOL allowBroadcast = TRUE;
    setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&allowBroadcast),
               sizeof(allowBroadcast));

    // A larger kernel queue absorbs short Wi-Fi/VPN scheduling pauses without
    // dropping the one datagram that contains the next required input frame.
    int socketBufferSize = 256 * 1024;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&socketBufferSize),
               sizeof(socketBufferSize));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&socketBufferSize),
               sizeof(socketBufferSize));

    // Windows otherwise reports an ICMP Port Unreachable from an early UDP
    // hello as WSAECONNRESET on the next recvfrom call. Handshake retries and
    // the session timeout are the correct place to handle that condition.
    BOOL enableConnectionReset = FALSE;
    DWORD bytesReturned = 0;
    WSAIoctl(socketHandle, SIO_UDP_CONNRESET, &enableConnectionReset,
             sizeof(enableConnectionReset), nullptr, 0, &bytesReturned,
             nullptr, nullptr);

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(config.listenPort);
    if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&local),
             sizeof(local)) == SOCKET_ERROR) {
        error = "unable to bind UDP socket";
        Stop();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(socketHandle, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        error = "unable to make UDP socket non-blocking";
        Stop();
        return false;
    }

    int localSize = sizeof(local);
    if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&local),
                    &localSize) == SOCKET_ERROR) {
        error = "unable to query UDP listen port";
        Stop();
        return false;
    }

    config_ = config;
    localPort_ = ntohs(local.sin_port);
    running_ = true;
    lastPeerTick_ = TickNow();
    transport_ = new (std::nothrow) AsyncTransport();
    if (transport_ == nullptr || !transport_->Start(socketHandle)) {
        error = "unable to start asynchronous UDP transport";
        Stop();
        return false;
    }
    if (config.role == LanSessionRole::kClient) {
        in_addr peer{};
        if (inet_pton(AF_INET, config.peerAddress.c_str(), &peer) != 1) {
            error = "peer_address must be an IPv4 address";
            Stop();
            return false;
        }
        peerAddress_ = peer.s_addr;
        peerPort_ = config.peerPort;
        peerKnown_ = true;
        if (!SendHello()) {
            error = "unable to send LAN hello packet";
            Stop();
            return false;
        }
    }
    return true;
}

void LanInputSession::Stop() noexcept {
    if (running_ && handshakeComplete_ && peerKnown_) {
        NetworkPacket packet{};
        packet.type = NetworkPacketType::kDisconnect;
        packet.flags = RoleFlag(config_.role);
        packet.sessionId = config_.sessionId;
        packet.configWord = timeline_;
        SendPacket(packet);
    }
    if (transport_ != nullptr) {
        transport_->Shutdown();
        delete transport_;
        transport_ = nullptr;
    }
    if (socket_ != kInvalidSocket) {
        closesocket(NativeSocket(socket_));
    }
    socket_ = kInvalidSocket;
    if (winsockStarted_) {
        WSACleanup();
    }
    config_ = {};
    sentFrames_.Clear();
    peerAddress_ = 0;
    peerPort_ = 0;
    localPort_ = 0;
    lastReceivedFrame_ = 0;
    lastObservedInputTimeline_ = 0;
    wrongTimelineInputPackets_ = 0;
    timeline_ = 1;
    hashFrames_.fill(0);
    hashes_.fill(0);
    hashValid_.fill(false);
    lastPeerTick_ = 0;
    lastHelloTick_ = 0;
    lastError_.clear();
    helloAttempts_ = 0;
    helloPacketsReceived_ = 0;
    helloAcksSent_ = 0;
    helloAcksReceived_ = 0;
    winsockStarted_ = false;
    running_ = false;
    peerKnown_ = false;
    handshakeComplete_ = false;
    localPaused_ = false;
    remotePaused_ = false;
    remotePauseFrame_ = 0;
    localControlSequence_ = 0;
    remoteControlSequence_ = 0;
    remoteDisconnected_ = false;
    localLobbyPlayer1_ = 0xFF;
    localLobbyPlayer2_ = 0xFF;
    remoteLobbyPlayer1_ = 0xFF;
    remoteLobbyPlayer2_ = 0xFF;
    remoteReadyPlayer1_ = 0xFF;
    remoteReadyPlayer2_ = 0xFF;
    remoteReadyResourceHash_ = 0;
    remoteRecommendedInputDelay_ = 1;
    remoteLobbySelectionValid_ = false;
    remoteReady_ = false;
    lobbyStartReceived_ = false;
    peerWasDiscovered_ = false;
    lobbyStartTimeline_ = 0;
    lobbyStartGameRng_ = {};
    lobbyStartInputDelay_ = 1;
    lobbyStartCoopRules_ = 0;
    sentTickFrames_.fill(0);
    sentTicks_.fill(0);
    lastAcknowledgedFrame_ = 0;
    smoothedRttMs_ = 0;
    jitterMs_ = 0;
    lastProbeTick_ = 0;
    lastProbeToken_ = 0;
}

void LanInputSession::ResetTimeline(uint32_t timeline) noexcept {
    timeline_ = timeline == 0 ? 1 : timeline;
    sentFrames_.Clear();
    lastReceivedFrame_ = 0;
    lastObservedInputTimeline_ = 0;
    wrongTimelineInputPackets_ = 0;
    hashFrames_.fill(0);
    hashes_.fill(0);
    hashValid_.fill(false);
    sentTickFrames_.fill(0);
    sentTicks_.fill(0);
    lastAcknowledgedFrame_ = 0;
    smoothedRttMs_ = 0;
    jitterMs_ = 0;
    localControlSequence_ = 0;
    remoteControlSequence_ = 0;
}

void LanInputSession::ResetLobby() noexcept {
    localLobbyPlayer1_ = 0xFF;
    localLobbyPlayer2_ = 0xFF;
    remoteLobbyPlayer1_ = 0xFF;
    remoteLobbyPlayer2_ = 0xFF;
    remoteReadyPlayer1_ = 0xFF;
    remoteReadyPlayer2_ = 0xFF;
    remoteReadyResourceHash_ = 0;
    remoteRecommendedInputDelay_ = 1;
    remoteLobbySelectionValid_ = false;
    remoteReady_ = false;
    lobbyStartReceived_ = false;
    lobbyStartTimeline_ = 0;
    lobbyStartGameRng_ = {};
    lobbyStartInputDelay_ = 1;
    lobbyStartCoopRules_ = 0;
}

bool LanInputSession::Poll(FrameInputBuffer& remoteFrames) noexcept {
    if (!running_) {
        return false;
    }
    if (config_.role == LanSessionRole::kClient && !lobbyStartReceived_ &&
        TickNow() - lastHelloTick_ >= 100 && !SendHello()) {
        return false;
    }

    if (transport_ == nullptr) {
        lastError_ = "asynchronous UDP transport is unavailable";
        return false;
    }
    const LONG receiveError =
        InterlockedCompareExchange(&transport_->receiveError, 0, 0);
    if (receiveError != 0) {
        lastError_ = SocketError("UDP receive failed", receiveError);
        return false;
    }

    AsyncTransport::Datagram datagram{};
    while (transport_->Pop(datagram)) {
        NetworkPacket packet{};
        const uint32_t sourceAddress = datagram.sourceAddress;
        const uint16_t sourcePort = datagram.sourcePort;
        if (!DecodeNetworkPacket(datagram.bytes.data(),
                                 static_cast<size_t>(datagram.size), packet) ||
            packet.sessionId != config_.sessionId) {
            continue;
        }

        if (packet.type == NetworkPacketType::kHello &&
            config_.role == LanSessionRole::kHost &&
            packet.flags == RoleFlag(LanSessionRole::kClient) &&
            packet.player1Mask == PlayerForRole(LanSessionRole::kClient)) {
            // A client sends the same Hello by direct unicast and through LAN
            // discovery.  Once one route completes the handshake, do not let
            // a later duplicate from another local/VPN adapter replace the
            // established endpoint.  Otherwise all subsequent input packets
            // from the original endpoint are silently rejected by IsPeer.
            if (handshakeComplete_ && !IsPeer(sourceAddress, sourcePort)) {
                continue;
            }
            ++helloPacketsReceived_;
            if (packet.configWord != ConfigWord(config_)) {
                lastError_ = "room network settings do not match";
                SendReject(sourceAddress, sourcePort, 1);
                continue;
            }
            peerAddress_ = sourceAddress;
            peerPort_ = sourcePort;
            peerKnown_ = true;
            handshakeComplete_ = true;
            lastPeerTick_ = TickNow();
            lastProbeToken_ = packet.controlValue;
            if (!SendHelloAck()) {
                return false;
            }
            continue;
        }
        if (packet.type == NetworkPacketType::kHelloAck &&
            config_.role == LanSessionRole::kClient &&
            packet.flags == RoleFlag(LanSessionRole::kHost) &&
            packet.player1Mask == PlayerForRole(LanSessionRole::kHost) &&
            packet.configWord == ConfigWord(config_) &&
            (!handshakeComplete_ || IsPeer(sourceAddress, sourcePort))) {
            peerWasDiscovered_ =
                sourceAddress != peerAddress_ || sourcePort != peerPort_;
            peerAddress_ = sourceAddress;
            peerPort_ = sourcePort;
            peerKnown_ = true;
            handshakeComplete_ = true;
            lastPeerTick_ = TickNow();
            ++helloAcksReceived_;
            if (packet.controlValue != 0) {
                const uint32_t now = static_cast<uint32_t>(TickNow());
                const uint32_t sample = now - packet.controlValue;
                if (sample < 60000U) {
                    if (smoothedRttMs_ == 0) {
                        smoothedRttMs_ = std::max<uint32_t>(sample, 1U);
                        jitterMs_ = 0;
                    } else {
                        const uint32_t deviation = sample > smoothedRttMs_
                            ? sample - smoothedRttMs_
                            : smoothedRttMs_ - sample;
                        jitterMs_ = (jitterMs_ * 7U + deviation) / 8U;
                        smoothedRttMs_ = (smoothedRttMs_ * 7U + sample) / 8U;
                    }
                }
            }
            continue;
        }
        if (!IsPeer(sourceAddress, sourcePort)) {
            continue;
        }
        lastPeerTick_ = TickNow();
        remoteDisconnected_ = false;
        if (packet.type == NetworkPacketType::kReject) {
            lastError_ = packet.controlValue == 1
                ? "room network settings do not match"
                : "connection rejected by host";
            continue;
        }
        const bool remoteSlotIsScoped =
            config_.role == LanSessionRole::kHost
                ? packet.player1Mask == 0
                : packet.player2Mask == 0;
        if (packet.type == NetworkPacketType::kInput && handshakeComplete_ &&
            packet.flags == RoleFlag(RemoteRole(config_.role)) &&
            remoteSlotIsScoped) {
            ObserveAcknowledgement(packet.acknowledgedFrame);
            lastObservedInputTimeline_ = packet.configWord;
            if (packet.configWord != timeline_) {
                ++wrongTimelineInputPackets_;
                continue;
            }
            const bool remoteIsPlayer1 =
                config_.role == LanSessionRole::kClient;
            for (uint32_t age = 0; age < packet.inputCount; ++age) {
                const uint32_t frame = packet.frame - age;
                const uint32_t mask = packet.inputMasks[age];
                const FrameInput input{
                    frame,
                    remoteIsPlayer1 ? mask : 0,
                    remoteIsPlayer1 ? 0 : mask};
                if (remoteFrames.Push(input) && frame > lastReceivedFrame_) {
                    lastReceivedFrame_ = frame;
                }
            }
            continue;
        }
        if (packet.type == NetworkPacketType::kControl &&
            handshakeComplete_ && packet.configWord == timeline_ &&
            packet.flags == RoleFlag(RemoteRole(config_.role)) &&
            packet.acknowledgedFrame >= remoteControlSequence_) {
            remoteControlSequence_ = packet.acknowledgedFrame;
            remotePaused_ =
                packet.controlValue ==
                static_cast<uint32_t>(NetworkControl::kPaused);
            remotePauseFrame_ = packet.frame;
            continue;
        }
        if (packet.type == NetworkPacketType::kStateHash &&
            handshakeComplete_ && packet.configWord == timeline_ &&
            packet.frame != 0) {
            const size_t index = static_cast<size_t>(
                packet.frame % FrameInputBuffer::kCapacity);
            hashFrames_[index] = packet.frame;
            hashes_[index] = packet.stateHash;
            hashValid_[index] = true;
            continue;
        }
        if (packet.type == NetworkPacketType::kLobbySelection &&
            handshakeComplete_ && packet.configWord == 0 &&
            packet.flags == RoleFlag(RemoteRole(config_.role))) {
            uint8_t player1 = 0xFF;
            uint8_t player2 = 0xFF;
            if (UnpackLobbyAirframes(packet.controlValue, player1, player2)) {
                remoteLobbyPlayer1_ = player1;
                remoteLobbyPlayer2_ = player2;
                remoteLobbySelectionValid_ = true;
            }
            continue;
        }
        if (packet.type == NetworkPacketType::kLobbyReady &&
            handshakeComplete_ && packet.configWord == 0 &&
            packet.flags == RoleFlag(RemoteRole(config_.role))) {
            uint8_t player1 = 0xFF;
            uint8_t player2 = 0xFF;
            if (UnpackLobbyAirframes(packet.controlValue, player1, player2)) {
                remoteReadyPlayer1_ = player1;
                remoteReadyPlayer2_ = player2;
                remoteReadyResourceHash_ = packet.stateHash;
                remoteRecommendedInputDelay_ = static_cast<uint8_t>(
                    std::clamp<uint32_t>(packet.frame, 1U, 12U));
                remoteReady_ = true;
            }
            continue;
        }
        if (packet.type == NetworkPacketType::kLobbyStart &&
            handshakeComplete_ && packet.configWord != 0 &&
            packet.flags == RoleFlag(LanSessionRole::kHost)) {
            lobbyStartTimeline_ = packet.configWord;
            lobbyStartGameRng_ = packet.gameRng;
            lobbyStartInputDelay_ = static_cast<uint8_t>(
                std::clamp<uint32_t>(packet.frame, 1U, 12U));
            lobbyStartCoopRules_ = static_cast<uint8_t>(
                packet.controlValue & 0xFFU);
            lobbyStartReceived_ = true;
            continue;
        }
        if (packet.type == NetworkPacketType::kDisconnect &&
            packet.configWord == timeline_) {
            remoteDisconnected_ = true;
            lastError_ = "remote player disconnected";
        }
    }
    return true;
}

bool LanInputSession::WaitForIncoming(uint32_t timeoutMs) const noexcept {
    if (!running_ || transport_ == nullptr ||
        transport_->incomingEvent == nullptr) {
        return false;
    }
    return WaitForSingleObject(transport_->incomingEvent, timeoutMs) ==
           WAIT_OBJECT_0;
}

bool LanInputSession::SetLocalPaused(bool paused,
                                     uint32_t effectiveFrame) noexcept {
    if (!running_ || !handshakeComplete_) {
        return false;
    }
    localPaused_ = paused;
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kControl;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.frame = effectiveFrame;
    packet.configWord = timeline_;
    packet.acknowledgedFrame = ++localControlSequence_;
    packet.controlValue = static_cast<uint32_t>(
        localPaused_ ? NetworkControl::kPaused : NetworkControl::kRunning);
    return SendPacket(packet);
}

bool LanInputSession::SendStateHash(uint32_t frame, uint64_t hash) noexcept {
    if (!running_ || !handshakeComplete_ || frame == 0) {
        return false;
    }
    NetworkPacket packet = MakeStateHashPacket(config_.sessionId, frame, hash);
    packet.flags = RoleFlag(config_.role);
    packet.configWord = timeline_;
    return SendPacket(packet);
}

bool LanInputSession::SendLobbySelection(uint8_t player1Airframe,
                                         uint8_t player2Airframe) noexcept {
    if (!running_ || !handshakeComplete_ ||
        (player1Airframe != 0xFF && player1Airframe >= 6) ||
        (player2Airframe != 0xFF && player2Airframe >= 6)) {
        return false;
    }
    localLobbyPlayer1_ = player1Airframe;
    localLobbyPlayer2_ = player2Airframe;
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kLobbySelection;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.controlValue = PackLobbyAirframes(player1Airframe, player2Airframe);
    return SendPacket(packet);
}

bool LanInputSession::GetRemoteLobbySelection(
    uint8_t& player1Airframe, uint8_t& player2Airframe) const noexcept {
    if (!remoteLobbySelectionValid_) {
        return false;
    }
    player1Airframe = remoteLobbyPlayer1_;
    player2Airframe = remoteLobbyPlayer2_;
    return true;
}

bool LanInputSession::SendLobbyReady(uint8_t player1Airframe,
                                     uint8_t player2Airframe,
                                     uint64_t resourceHash,
                                     uint8_t recommendedInputDelay) noexcept {
    if (!running_ || !handshakeComplete_ || player1Airframe >= 6 ||
        player2Airframe >= 6) {
        return false;
    }
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kLobbyReady;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.controlValue = PackLobbyAirframes(player1Airframe, player2Airframe);
    packet.stateHash = resourceHash;
    packet.frame = std::clamp<uint32_t>(recommendedInputDelay, 1U, 12U);
    return SendPacket(packet);
}

bool LanInputSession::RemoteLobbyReady(
    uint8_t& player1Airframe, uint8_t& player2Airframe,
    uint64_t& resourceHash, uint8_t& recommendedInputDelay) const noexcept {
    if (!remoteReady_) {
        return false;
    }
    player1Airframe = remoteReadyPlayer1_;
    player2Airframe = remoteReadyPlayer2_;
    resourceHash = remoteReadyResourceHash_;
    recommendedInputDelay = remoteRecommendedInputDelay_;
    return true;
}

bool LanInputSession::SendLobbyStart(uint32_t timeline,
                                     const GameRngState& gameRng,
                                     uint8_t negotiatedInputDelay,
                                     uint8_t coopRules) noexcept {
    if (!running_ || !handshakeComplete_ || timeline == 0) {
        return false;
    }
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kLobbyStart;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.configWord = timeline;
    packet.frame = std::clamp<uint32_t>(negotiatedInputDelay, 1U, 12U);
    packet.controlValue = coopRules;
    packet.gameRng = gameRng;
    return SendPacket(packet);
}

bool LanInputSession::LobbyStartReceived(uint32_t& timeline,
                                         GameRngState& gameRng,
                                         uint8_t& negotiatedInputDelay,
                                         uint8_t& coopRules) const noexcept {
    if (!lobbyStartReceived_) {
        return false;
    }
    timeline = lobbyStartTimeline_;
    gameRng = lobbyStartGameRng_;
    negotiatedInputDelay = lobbyStartInputDelay_;
    coopRules = lobbyStartCoopRules_;
    return true;
}

bool LanInputSession::FindRemoteStateHash(uint32_t frame,
                                          uint64_t& hash) const noexcept {
    if (frame == 0) {
        return false;
    }
    const size_t index = static_cast<size_t>(
        frame % FrameInputBuffer::kCapacity);
    if (!hashValid_[index] || hashFrames_[index] != frame) {
        return false;
    }
    hash = hashes_[index];
    return true;
}

bool LanInputSession::SendInput(const FrameInput& input) noexcept {
    if (!running_ || !handshakeComplete_ || !input.IsValid() ||
        !sentFrames_.Push(input)) {
        return false;
    }
    NetworkPacket packet = MakeInputPacket(config_.sessionId, input,
                                            lastReceivedFrame_);
    packet.flags = RoleFlag(config_.role);
    packet.configWord = timeline_;
    packet.inputCount = 0;
    // Filling more history does not add datagrams: the v6 packet reserves the
    // bytes either way. Thirty-two frames cover the worst lead possible with
    // the supported 12-frame input delay and repair old single-frame holes.
    const uint32_t historyWindow =
        static_cast<uint32_t>(packet.inputMasks.size());
    for (uint32_t age = 0; age < historyWindow && age < input.frame; ++age) {
        FrameInput previous{};
        if (!sentFrames_.Find(input.frame - age, previous)) {
            break;
        }
        packet.inputMasks[packet.inputCount++] = static_cast<uint8_t>(
            config_.role == LanSessionRole::kHost
                ? previous.player1Mask
                : previous.player2Mask);
    }
    if (packet.inputCount == 0) {
        return false;
    }
    const size_t tickIndex = static_cast<size_t>(
        input.frame % FrameInputBuffer::kCapacity);
    if (sentTickFrames_[tickIndex] != input.frame) {
        sentTickFrames_[tickIndex] = input.frame;
        sentTicks_[tickIndex] = TickNow();
    }
    const bool sent = SendPacket(packet);
    if (sent && transport_ != nullptr) {
        std::array<uint8_t, kNetworkPacketSize> bytes{};
        if (EncodeNetworkPacket(packet, bytes)) {
            transport_->PublishResend(bytes, peerAddress_, peerPort_);
        }
    }
    return sent;
}

bool LanInputSession::IsRunning() const noexcept {
    return running_;
}

bool LanInputSession::HandshakeComplete() const noexcept {
    return handshakeComplete_;
}

uint16_t LanInputSession::LocalPort() const noexcept {
    return localPort_;
}

uint32_t LanInputSession::LastReceivedFrame() const noexcept {
    return lastReceivedFrame_;
}

uint32_t LanInputSession::LastObservedInputTimeline() const noexcept {
    return lastObservedInputTimeline_;
}

uint32_t LanInputSession::WrongTimelineInputPackets() const noexcept {
    return wrongTimelineInputPackets_;
}

bool LanInputSession::RemotePaused() const noexcept {
    return remotePaused_;
}

uint32_t LanInputSession::RemotePauseFrame() const noexcept {
    return remotePauseFrame_;
}

bool LanInputSession::RemoteDisconnected() const noexcept {
    return remoteDisconnected_;
}

uint64_t LanInputSession::PeerSilentMilliseconds() const noexcept {
    return lastPeerTick_ == 0 ? 0 : TickNow() - lastPeerTick_;
}

uint32_t LanInputSession::SmoothedRttMilliseconds() const noexcept {
    return smoothedRttMs_;
}

uint32_t LanInputSession::JitterMilliseconds() const noexcept {
    return jitterMs_;
}

uint8_t LanInputSession::RecommendedInputDelay() const noexcept {
    if (smoothedRttMs_ == 0) {
        return std::clamp<uint8_t>(config_.inputDelay, 1, 12);
    }
    // Cover estimated one-way latency plus two jitter deviations and one
    // scheduling frame. Integer arithmetic uses 17 ms as a 60 Hz frame.
    const uint32_t budgetMs = smoothedRttMs_ / 2U + jitterMs_ * 2U;
    const uint32_t frames = (budgetMs + 16U) / 17U + 1U;
    return static_cast<uint8_t>(std::clamp<uint32_t>(frames, 1, 12));
}

uint32_t LanInputSession::TransportQueueDrops() const noexcept {
    return transport_ == nullptr ? 0U : static_cast<uint32_t>(
        InterlockedCompareExchange(&transport_->queueDrops, 0, 0));
}

uint32_t LanInputSession::TransportResends() const noexcept {
    return transport_ == nullptr ? 0U : static_cast<uint32_t>(
        InterlockedCompareExchange(&transport_->resends, 0, 0));
}

const std::string& LanInputSession::LastError() const noexcept {
    return lastError_;
}

uint32_t LanInputSession::HelloAttempts() const noexcept {
    return helloAttempts_;
}

uint32_t LanInputSession::HelloPacketsReceived() const noexcept {
    return helloPacketsReceived_;
}

uint32_t LanInputSession::HelloAcksSent() const noexcept {
    return helloAcksSent_;
}

uint32_t LanInputSession::HelloAcksReceived() const noexcept {
    return helloAcksReceived_;
}

bool LanInputSession::PeerWasDiscovered() const noexcept {
    return peerWasDiscovered_;
}

bool LanInputSession::SendHello() noexcept {
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kHello;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.frame = static_cast<uint32_t>(TickNow());
    packet.player1Mask = config_.localPlayer;
    packet.configWord = ConfigWord(config_);
    packet.controlValue = packet.frame;
    lastProbeToken_ = packet.controlValue;
    lastProbeTick_ = static_cast<uint32_t>(TickNow());
    const bool unicastSent = SendPacketTo(packet, peerAddress_, peerPort_, false);
    // Give the configured direct endpoint a short head start.  A client may
    // have several adapters (Wi-Fi, VPN, Radmin); broadcasting on the very
    // first datagram can make the host lock onto a different source address
    // before the direct route has had a chance to complete.
    bool discoverySent = false;
    const bool discoveryAllowed = !handshakeComplete_ && helloAttempts_ >= 2;
    if (config_.enableLanDiscovery && discoveryAllowed) {
        discoverySent =
            SendPacketTo(packet, INADDR_BROADCAST, peerPort_, false);

        // Limited broadcast can be routed to the wrong adapter when a VPN or
        // virtual NIC is present. Send to each IPv4 interface's directed
        // broadcast address as well.
        std::array<INTERFACE_INFO, 32> interfaces{};
        DWORD bytesReturned = 0;
        if (WSAIoctl(NativeSocket(socket_), SIO_GET_INTERFACE_LIST, nullptr, 0,
                     interfaces.data(),
                     static_cast<DWORD>(sizeof(interfaces)), &bytesReturned,
                     nullptr, nullptr) == 0) {
            const size_t count = bytesReturned / sizeof(INTERFACE_INFO);
            for (size_t index = 0;
                 index < count && index < interfaces.size(); ++index) {
                const sockaddr_in& address =
                    interfaces[index].iiBroadcastAddress.AddressIn;
                if (address.sin_family != AF_INET) {
                    continue;
                }
                discoverySent = SendPacketTo(
                    packet, address.sin_addr.s_addr, peerPort_, false) ||
                    discoverySent;
            }
        }
    }
    const bool sent = unicastSent || discoverySent;
    if (sent) {
        ++helloAttempts_;
        lastHelloTick_ = TickNow();
    } else {
        lastError_ = "unable to send LAN hello by unicast or broadcast";
    }
    return sent;
}

bool LanInputSession::SendHelloAck() noexcept {
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kHelloAck;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.frame = static_cast<uint32_t>(TickNow());
    packet.player1Mask = config_.localPlayer;
    packet.configWord = ConfigWord(config_);
    packet.controlValue = lastProbeToken_;
    const bool sent = SendPacket(packet);
    if (sent) {
        ++helloAcksSent_;
    }
    return sent;
}

bool LanInputSession::SendReject(uint32_t address, uint16_t port,
                                 uint32_t reason) noexcept {
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kReject;
    packet.flags = RoleFlag(config_.role);
    packet.sessionId = config_.sessionId;
    packet.controlValue = reason;
    return SendPacketTo(packet, address, port);
}

bool LanInputSession::SendPacket(const NetworkPacket& packet) noexcept {
    return peerKnown_ && SendPacketTo(packet, peerAddress_, peerPort_);
}

bool LanInputSession::SendPacketTo(const NetworkPacket& packet, uint32_t address,
                                   uint16_t port, bool reportError) noexcept {
    std::array<uint8_t, kNetworkPacketSize> bytes{};
    if (!running_ || socket_ == kInvalidSocket ||
        !EncodeNetworkPacket(packet, bytes)) {
        return false;
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = address;
    destination.sin_port = htons(port);
    const int sent = sendto(
        NativeSocket(socket_), reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), 0,
        reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    if (sent == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (IsTransientUdpError(error)) {
            return true;
        }
        if (reportError) {
            lastError_ = SocketError("UDP send failed", error);
        }
        return false;
    }
    if (sent != static_cast<int>(bytes.size())) {
        if (reportError) {
            lastError_ = "UDP send returned a partial datagram";
        }
        return false;
    }
    return true;
}

bool LanInputSession::IsPeer(uint32_t address, uint16_t port) const noexcept {
    return peerKnown_ && peerAddress_ == address && peerPort_ == port;
}

void LanInputSession::ObserveAcknowledgement(uint32_t frame) noexcept {
    if (frame == 0 || frame <= lastAcknowledgedFrame_) {
        return;
    }
    const size_t index = static_cast<size_t>(
        frame % FrameInputBuffer::kCapacity);
    if (sentTickFrames_[index] != frame || sentTicks_[index] == 0) {
        return;
    }
    const uint64_t now = TickNow();
    if (now < sentTicks_[index]) {
        return;
    }
    const uint32_t sample = static_cast<uint32_t>(std::min<uint64_t>(
        now - sentTicks_[index], 60000));
    lastAcknowledgedFrame_ = frame;
    if (smoothedRttMs_ == 0) {
        smoothedRttMs_ = std::max<uint32_t>(sample, 1);
        jitterMs_ = 0;
        return;
    }
    const uint32_t deviation = sample > smoothedRttMs_
        ? sample - smoothedRttMs_
        : smoothedRttMs_ - sample;
    jitterMs_ = (jitterMs_ * 7U + deviation) / 8U;
    smoothedRttMs_ = (smoothedRttMs_ * 7U + sample) / 8U;
}

}  // namespace coop::th12
