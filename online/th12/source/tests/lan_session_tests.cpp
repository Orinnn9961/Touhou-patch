#include "lan_session.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FUNCTION__ << ": check failed at line "         \
                      << __LINE__ << ": " #condition "\n";                 \
            return false;                                                    \
        }                                                                    \
    } while (false)

bool LoopbackHandshakeAndInputExchange() {
    constexpr uint32_t kSessionId = 0x12061206;
    coop::th12::LanInputSession host;
    coop::th12::LanInputSession client;
    std::string error;

    coop::th12::LanSessionConfig hostConfig{};
    hostConfig.role = coop::th12::LanSessionRole::kHost;
    hostConfig.sessionId = kSessionId;
    hostConfig.listenPort = 0;
    CHECK(host.Start(hostConfig, error));
    CHECK(host.LocalPort() != 0);

    coop::th12::LanSessionConfig clientConfig{};
    clientConfig.role = coop::th12::LanSessionRole::kClient;
    clientConfig.sessionId = kSessionId;
    clientConfig.listenPort = 0;
    clientConfig.peerAddress = "127.0.0.1";
    clientConfig.peerPort = host.LocalPort();
    clientConfig.localPlayer = 2;
    CHECK(client.Start(clientConfig, error));

    coop::th12::FrameInputBuffer hostFrames;
    coop::th12::FrameInputBuffer clientFrames;
    for (int attempt = 0;
         attempt < 100 &&
         (!host.HandshakeComplete() || !client.HandshakeComplete());
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.HandshakeComplete());
    CHECK(client.HandshakeComplete());

    // v7 keeps lightweight Hello/Ack probes alive behind the lobby barrier so
    // the client can recommend a safe delay before simulation frame one.
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    for (int attempt = 0;
         attempt < 100 && client.SmoothedRttMilliseconds() == 0; ++attempt) {
        CHECK(client.Poll(clientFrames));
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(client.SmoothedRttMilliseconds() > 0);

    CHECK(host.SendLobbySelection(1, 0xFF));
    CHECK(client.SendLobbySelection(0xFF, 4));
    uint8_t remotePlayer1 = 0xFF;
    uint8_t remotePlayer2 = 0xFF;
    for (int attempt = 0;
         attempt < 100 &&
         (!host.GetRemoteLobbySelection(remotePlayer1, remotePlayer2) ||
          !client.GetRemoteLobbySelection(remotePlayer1, remotePlayer2));
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.GetRemoteLobbySelection(remotePlayer1, remotePlayer2));
    CHECK(remotePlayer1 == 0xFF);
    CHECK(remotePlayer2 == 4);
    CHECK(client.GetRemoteLobbySelection(remotePlayer1, remotePlayer2));
    CHECK(remotePlayer1 == 1);
    CHECK(remotePlayer2 == 0xFF);

    constexpr uint64_t kResourceHash = 0xA512B512C512D512ULL;
    CHECK(host.SendLobbyReady(1, 4, kResourceHash, 2));
    CHECK(client.SendLobbyReady(1, 4, kResourceHash, 5));
    uint64_t remoteResourceHash = 0;
    uint8_t remoteRecommendedDelay = 0;
    for (int attempt = 0;
         attempt < 100 &&
         (!host.RemoteLobbyReady(remotePlayer1, remotePlayer2,
                                 remoteResourceHash, remoteRecommendedDelay) ||
          !client.RemoteLobbyReady(remotePlayer1, remotePlayer2,
                                   remoteResourceHash, remoteRecommendedDelay));
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(client.RemoteLobbyReady(remotePlayer1, remotePlayer2,
                                  remoteResourceHash, remoteRecommendedDelay));
    CHECK(remotePlayer1 == 1);
    CHECK(remotePlayer2 == 4);
    CHECK(remoteResourceHash == kResourceHash);
    CHECK(remoteRecommendedDelay == 2);
    CHECK(host.RemoteLobbyReady(remotePlayer1, remotePlayer2,
                                remoteResourceHash, remoteRecommendedDelay));
    CHECK(remoteRecommendedDelay == 5);
    const coop::th12::GameRngState startRng{{0x1234, 54321},
                                             {0xABCD, 9876}};
    constexpr uint8_t kCoopRules = 0x0F;
    CHECK(host.SendLobbyStart(1, startRng, 5, kCoopRules));
    uint32_t startTimeline = 0;
    coop::th12::GameRngState receivedStartRng{};
    uint8_t receivedInputDelay = 0;
    uint8_t receivedCoopRules = 0;
    for (int attempt = 0;
         attempt < 100 &&
         !client.LobbyStartReceived(startTimeline, receivedStartRng,
                                    receivedInputDelay, receivedCoopRules);
         ++attempt) {
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(client.LobbyStartReceived(startTimeline, receivedStartRng,
                                    receivedInputDelay, receivedCoopRules));
    CHECK(startTimeline == 1);
    CHECK(receivedStartRng == startRng);
    CHECK(receivedInputDelay == 5);
    CHECK(receivedCoopRules == kCoopRules);

    CHECK(client.SendInput({42, 0, 0x88}));
    CHECK(host.WaitForIncoming(100));
    coop::th12::FrameInput received{};
    for (int attempt = 0; attempt < 100 && !hostFrames.Find(42, received);
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(hostFrames.Find(42, received));
    CHECK(received.player1Mask == 0);
    CHECK(received.player2Mask == 0x88);
    CHECK(host.LastReceivedFrame() == 42);

    // A packet in the opposite direction acknowledges frame 42. This
    // exercises the RTT/jitter estimator without adding protocol traffic.
    CHECK(host.SendInput({42, 0x10, 0}));
    CHECK(client.WaitForIncoming(100));
    for (int attempt = 0; attempt < 100 &&
         client.SmoothedRttMilliseconds() == 0; ++attempt) {
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(client.SmoothedRttMilliseconds() > 0);
    CHECK(client.RecommendedInputDelay() >= 1);
    CHECK(client.RecommendedInputDelay() <= 12);

    // With no newer local input, the transport thread repairs loss without
    // requiring the simulation thread to call SendInput again.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(host.TransportResends() > 0);
    CHECK(host.TransportQueueDrops() == 0);

    // Each datagram carries the recent input window. Verify that a neutral
    // frame and non-neutral history survive the bundled transport together.
    for (uint32_t frame = 43; frame <= 50; ++frame) {
        CHECK(client.SendInput({frame, 0, frame == 47 ? 0x21U : 0U}));
    }
    for (int attempt = 0; attempt < 100 && !hostFrames.Find(50, received);
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(hostFrames.Find(47, received));
    CHECK(received.player2Mask == 0x21);
    CHECK(hostFrames.Find(50, received));
    CHECK(received.player2Mask == 0);
    CHECK(host.LastReceivedFrame() == 50);

    CHECK(client.SetLocalPaused(true, 50));
    for (int attempt = 0; attempt < 100 && !host.RemotePaused(); ++attempt) {
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.RemotePaused());
    CHECK(host.RemotePauseFrame() == 50);
    CHECK(client.SetLocalPaused(false, 50));
    for (int attempt = 0; attempt < 100 && host.RemotePaused(); ++attempt) {
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(!host.RemotePaused());

    constexpr uint64_t kHash = 0x123456789ABCDEF0ULL;
    CHECK(host.SendStateHash(120, kHash));
    uint64_t receivedHash = 0;
    for (int attempt = 0;
         attempt < 100 && !client.FindRemoteStateHash(120, receivedHash);
         ++attempt) {
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(client.FindRemoteStateHash(120, receivedHash));
    CHECK(receivedHash == kHash);

    host.ResetTimeline(2);
    CHECK(client.SendInput({7, 0, 0x10}));
    for (int attempt = 0; attempt < 20; ++attempt) {
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(!hostFrames.Find(7, received));
    client.ResetTimeline(2);
    CHECK(client.SendInput({7, 0, 0x20}));
    for (int attempt = 0; attempt < 100 && !hostFrames.Find(7, received);
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(hostFrames.Find(7, received));
    CHECK(received.player2Mask == 0x20);
    return true;
}

bool MismatchedRoomSettingsAreRejected() {
    constexpr uint32_t kSessionId = 0x99887766;
    coop::th12::LanInputSession host;
    coop::th12::LanInputSession client;
    std::string error;
    coop::th12::LanSessionConfig hostConfig{};
    hostConfig.sessionId = kSessionId;
    hostConfig.listenPort = 0;
    CHECK(host.Start(hostConfig, error));

    coop::th12::LanSessionConfig clientConfig{};
    clientConfig.role = coop::th12::LanSessionRole::kClient;
    clientConfig.sessionId = kSessionId;
    clientConfig.listenPort = 0;
    clientConfig.peerPort = host.LocalPort();
    clientConfig.localPlayer = 2;
    clientConfig.inputDelay = 4;
    CHECK(client.Start(clientConfig, error));

    coop::th12::FrameInputBuffer hostFrames;
    coop::th12::FrameInputBuffer clientFrames;
    for (int attempt = 0; attempt < 100 && client.LastError().empty();
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(!host.HandshakeComplete());
    CHECK(!client.HandshakeComplete());
    CHECK(!client.LastError().empty());
    return true;
}

bool ClientCanStartBeforeHost() {
    constexpr uint32_t kSessionId = 0x44556677;
    std::string error;

    // Reserve an ephemeral port, then close it so the client's first hello is
    // guaranteed to target a temporarily unopened UDP endpoint.
    coop::th12::LanInputSession reservation;
    coop::th12::LanSessionConfig reservationConfig{};
    reservationConfig.sessionId = kSessionId;
    reservationConfig.listenPort = 0;
    CHECK(reservation.Start(reservationConfig, error));
    const uint16_t delayedHostPort = reservation.LocalPort();
    CHECK(delayedHostPort != 0);
    reservation.Stop();

    coop::th12::LanInputSession client;
    coop::th12::LanSessionConfig clientConfig{};
    clientConfig.role = coop::th12::LanSessionRole::kClient;
    clientConfig.sessionId = kSessionId;
    clientConfig.listenPort = 0;
    clientConfig.peerAddress = "127.0.0.1";
    clientConfig.peerPort = delayedHostPort;
    clientConfig.localPlayer = 2;
    CHECK(client.Start(clientConfig, error));

    coop::th12::FrameInputBuffer clientFrames;
    for (int attempt = 0; attempt < 20; ++attempt) {
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    coop::th12::LanInputSession host;
    coop::th12::LanSessionConfig hostConfig{};
    hostConfig.sessionId = kSessionId;
    hostConfig.listenPort = delayedHostPort;
    CHECK(host.Start(hostConfig, error));

    coop::th12::FrameInputBuffer hostFrames;
    for (int attempt = 0;
         attempt < 600 &&
         (!host.HandshakeComplete() || !client.HandshakeComplete());
         ++attempt) {
        CHECK(host.Poll(hostFrames));
        CHECK(client.Poll(clientFrames));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(host.HandshakeComplete());
    CHECK(client.HandshakeComplete());
    return true;
}

bool SessionRoleFixesTheLogicalPlayerSlot() {
    coop::th12::LanSessionConfig host{};
    host.role = coop::th12::LanSessionRole::kHost;
    host.sessionId = 1;
    CHECK(host.IsValid());
    host.localPlayer = 2;
    CHECK(!host.IsValid());

    coop::th12::LanSessionConfig client{};
    client.role = coop::th12::LanSessionRole::kClient;
    client.sessionId = 1;
    client.peerPort = 28765;
    client.localPlayer = 2;
    CHECK(client.IsValid());
    client.localPlayer = 1;
    CHECK(!client.IsValid());
    return true;
}

}  // namespace

int main() {
    if (!LoopbackHandshakeAndInputExchange() ||
        !ClientCanStartBeforeHost() ||
        !SessionRoleFixesTheLogicalPlayerSlot() ||
        !MismatchedRoomSettingsAreRejected()) {
        return 1;
    }
    std::cout << "LAN session tests passed\n";
    return 0;
}
