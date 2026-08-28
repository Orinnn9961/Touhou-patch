#pragma once

#include "frame_input.h"
#include "game_rng_state.h"

#include <array>
#include <cstdint>
#include <string>

namespace coop::th12 {

enum class LanSessionRole : uint8_t {
    kHost = 1,
    kClient = 2,
};

struct LanSessionConfig {
    LanSessionRole role{LanSessionRole::kHost};
    uint32_t sessionId{};
    uint16_t listenPort{};
    std::string peerAddress{"127.0.0.1"};
    uint16_t peerPort{};
    uint8_t localPlayer{1};
    uint8_t inputDelay{2};
    uint8_t inputRedundancy{8};
    uint32_t disconnectTimeoutMs{15000};
    bool enableLanDiscovery{true};

    bool IsValid() const noexcept;
};

class LanInputSession {
public:
    LanInputSession() = default;
    ~LanInputSession();

    LanInputSession(const LanInputSession&) = delete;
    LanInputSession& operator=(const LanInputSession&) = delete;

    bool Start(const LanSessionConfig& config, std::string& error) noexcept;
    void Stop() noexcept;
    void ResetTimeline(uint32_t timeline) noexcept;
    void ResetLobby() noexcept;
    bool Poll(FrameInputBuffer& remoteFrames) noexcept;
    bool WaitForIncoming(uint32_t timeoutMs) const noexcept;
    bool SendInput(const FrameInput& input) noexcept;
    bool SetLocalPaused(bool paused, uint32_t effectiveFrame) noexcept;
    bool SendStateHash(uint32_t frame, uint64_t hash) noexcept;
    bool FindRemoteStateHash(uint32_t frame, uint64_t& hash) const noexcept;
    bool SendLobbySelection(uint8_t player1Airframe,
                            uint8_t player2Airframe) noexcept;
    bool GetRemoteLobbySelection(uint8_t& player1Airframe,
                                 uint8_t& player2Airframe) const noexcept;
    bool SendLobbyReady(uint8_t player1Airframe, uint8_t player2Airframe,
                        uint64_t resourceHash,
                        uint8_t recommendedInputDelay) noexcept;
    bool RemoteLobbyReady(uint8_t& player1Airframe, uint8_t& player2Airframe,
                          uint64_t& resourceHash,
                          uint8_t& recommendedInputDelay) const noexcept;
    bool SendLobbyStart(uint32_t timeline,
                        const GameRngState& gameRng,
                        uint8_t negotiatedInputDelay,
                        uint8_t coopRules) noexcept;
    bool LobbyStartReceived(uint32_t& timeline,
                            GameRngState& gameRng,
                            uint8_t& negotiatedInputDelay,
                            uint8_t& coopRules) const noexcept;

    bool IsRunning() const noexcept;
    bool HandshakeComplete() const noexcept;
    uint16_t LocalPort() const noexcept;
    uint32_t LastReceivedFrame() const noexcept;
    uint32_t LastObservedInputTimeline() const noexcept;
    uint32_t WrongTimelineInputPackets() const noexcept;
    bool RemotePaused() const noexcept;
    uint32_t RemotePauseFrame() const noexcept;
    bool RemoteDisconnected() const noexcept;
    uint64_t PeerSilentMilliseconds() const noexcept;
    uint32_t SmoothedRttMilliseconds() const noexcept;
    uint32_t JitterMilliseconds() const noexcept;
    uint8_t RecommendedInputDelay() const noexcept;
    uint32_t TransportQueueDrops() const noexcept;
    uint32_t TransportResends() const noexcept;
    const std::string& LastError() const noexcept;
    uint32_t HelloAttempts() const noexcept;
    uint32_t HelloPacketsReceived() const noexcept;
    uint32_t HelloAcksSent() const noexcept;
    uint32_t HelloAcksReceived() const noexcept;
    bool PeerWasDiscovered() const noexcept;

private:
    bool SendHello() noexcept;
    bool SendHelloAck() noexcept;
    bool SendReject(uint32_t address, uint16_t port,
                    uint32_t reason) noexcept;
    bool SendPacket(const struct NetworkPacket& packet) noexcept;
    bool SendPacketTo(const struct NetworkPacket& packet, uint32_t address,
                      uint16_t port, bool reportError = true) noexcept;
    bool IsPeer(uint32_t address, uint16_t port) const noexcept;
    void ObserveAcknowledgement(uint32_t frame) noexcept;

    static constexpr uintptr_t kInvalidSocket = ~static_cast<uintptr_t>(0);

    struct AsyncTransport;

    LanSessionConfig config_{};
    FrameInputBuffer sentFrames_{};
    uintptr_t socket_{kInvalidSocket};
    uint32_t peerAddress_{};
    uint16_t peerPort_{};
    uint16_t localPort_{};
    uint32_t lastReceivedFrame_{};
    uint32_t lastObservedInputTimeline_{};
    uint32_t wrongTimelineInputPackets_{};
    uint32_t timeline_{1};
    std::array<uint32_t, FrameInputBuffer::kCapacity> hashFrames_{};
    std::array<uint64_t, FrameInputBuffer::kCapacity> hashes_{};
    std::array<bool, FrameInputBuffer::kCapacity> hashValid_{};
    uint64_t lastPeerTick_{};
    uint64_t lastHelloTick_{};
    std::string lastError_{};
    uint32_t helloAttempts_{};
    uint32_t helloPacketsReceived_{};
    uint32_t helloAcksSent_{};
    uint32_t helloAcksReceived_{};
    bool winsockStarted_{};
    bool running_{};
    bool peerKnown_{};
    bool handshakeComplete_{};
    bool localPaused_{};
    bool remotePaused_{};
    uint32_t remotePauseFrame_{};
    uint32_t localControlSequence_{};
    uint32_t remoteControlSequence_{};
    bool remoteDisconnected_{};
    uint8_t localLobbyPlayer1_{0xFF};
    uint8_t localLobbyPlayer2_{0xFF};
    uint8_t remoteLobbyPlayer1_{0xFF};
    uint8_t remoteLobbyPlayer2_{0xFF};
    uint8_t remoteReadyPlayer1_{0xFF};
    uint8_t remoteReadyPlayer2_{0xFF};
    uint64_t remoteReadyResourceHash_{};
    uint8_t remoteRecommendedInputDelay_{1};
    bool remoteLobbySelectionValid_{};
    bool remoteReady_{};
    bool lobbyStartReceived_{};
    bool peerWasDiscovered_{};
    uint32_t lobbyStartTimeline_{};
    GameRngState lobbyStartGameRng_{};
    uint8_t lobbyStartInputDelay_{1};
    uint8_t lobbyStartCoopRules_{};
    AsyncTransport* transport_{};
    std::array<uint32_t, FrameInputBuffer::kCapacity> sentTickFrames_{};
    std::array<uint64_t, FrameInputBuffer::kCapacity> sentTicks_{};
    uint32_t lastAcknowledgedFrame_{};
    uint32_t smoothedRttMs_{};
    uint32_t jitterMs_{};
    uint32_t lastProbeTick_{};
    uint32_t lastProbeToken_{};
};

}  // namespace coop::th12
