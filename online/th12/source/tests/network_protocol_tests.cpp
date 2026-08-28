#include "network_protocol.h"

#include <array>
#include <iostream>

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FUNCTION__ << ": check failed at line "         \
                      << __LINE__ << ": " #condition "\n";                 \
            return false;                                                    \
        }                                                                    \
    } while (false)

bool InputPacketRoundTrips() {
    const coop::th12::FrameInput input{120, 0, 0xA0};
    const coop::th12::NetworkPacket source =
        coop::th12::MakeInputPacket(0x12345678, input, 118);
    std::array<uint8_t, coop::th12::kNetworkPacketSize> bytes{};
    CHECK(coop::th12::EncodeNetworkPacket(source, bytes));

    coop::th12::NetworkPacket decoded{};
    CHECK(coop::th12::DecodeNetworkPacket(bytes.data(), bytes.size(), decoded));
    CHECK(decoded.type == coop::th12::NetworkPacketType::kInput);
    CHECK(decoded.sessionId == 0x12345678);
    CHECK(decoded.frame == 120);
    CHECK(decoded.player1Mask == 0);
    CHECK(decoded.player2Mask == 0xA0);
    CHECK(decoded.acknowledgedFrame == 118);
    CHECK(decoded.inputCount == 1);
    CHECK(decoded.inputMasks[0] == 0xA0);
    return true;
}

bool InputBundleRoundTrips() {
    coop::th12::NetworkPacket source{};
    source.type = coop::th12::NetworkPacketType::kInput;
    source.flags = 2;
    source.sessionId = 0x55667788;
    source.frame = 200;
    source.player2Mask = 0x8A;
    source.acknowledgedFrame = 197;
    source.configWord = 4;
    source.inputCount = static_cast<uint8_t>(source.inputMasks.size());
    for (size_t i = 0; i < source.inputMasks.size(); ++i) {
        source.inputMasks[i] = static_cast<uint8_t>(0x80U | i);
    }
    std::array<uint8_t, coop::th12::kNetworkPacketSize> bytes{};
    CHECK(coop::th12::EncodeNetworkPacket(source, bytes));
    coop::th12::NetworkPacket decoded{};
    CHECK(coop::th12::DecodeNetworkPacket(bytes.data(), bytes.size(), decoded));
    CHECK(decoded.type == coop::th12::NetworkPacketType::kInput);
    CHECK(decoded.frame == 200);
    CHECK(decoded.acknowledgedFrame == 197);
    CHECK(decoded.configWord == 4);
    CHECK(decoded.inputCount == source.inputMasks.size());
    CHECK(decoded.inputMasks == source.inputMasks);
    return true;
}

bool CorruptionAndInvalidFieldsAreRejected() {
    std::array<uint8_t, coop::th12::kNetworkPacketSize> bytes{};
    CHECK(coop::th12::EncodeNetworkPacket(
        {coop::th12::NetworkPacketType::kHello, 1, 7, 3, 1, 0, 0,
         coop::th12::PackNetworkConfig(3, 8), 0, 0}, bytes));
    bytes[16] ^= 0x40;
    coop::th12::NetworkPacket decoded{};
    CHECK(!coop::th12::DecodeNetworkPacket(bytes.data(), bytes.size(), decoded));
    CHECK(!coop::th12::DecodeNetworkPacket(bytes.data(), bytes.size() - 1,
                                           decoded));
    CHECK(!coop::th12::EncodeNetworkPacket(
        {coop::th12::NetworkPacketType::kInput, 0, 0, 1, 0, 0, 0}, bytes));
    CHECK(!coop::th12::EncodeNetworkPacket(
        {coop::th12::NetworkPacketType::kInput, 0, 1, 1, 0x100, 0, 0},
        bytes));
    return true;
}

bool StateHashesRoundTripWithoutTruncation() {
    auto source = coop::th12::MakeStateHashPacket(
        9, 240, 0xFEDCBA9876543210ULL);
    source.flags = 2;
    std::array<uint8_t, coop::th12::kNetworkPacketSize> bytes{};
    CHECK(coop::th12::EncodeNetworkPacket(source, bytes));
    coop::th12::NetworkPacket decoded{};
    CHECK(coop::th12::DecodeNetworkPacket(bytes.data(), bytes.size(), decoded));
    CHECK(decoded.type == coop::th12::NetworkPacketType::kStateHash);
    CHECK(decoded.frame == 240);
    CHECK(decoded.stateHash == 0xFEDCBA9876543210ULL);
    return true;
}

bool LobbyStartRngStateRoundTripsWithoutTruncation() {
    coop::th12::NetworkPacket source{};
    source.type = coop::th12::NetworkPacketType::kLobbyStart;
    source.sessionId = 7;
    source.configWord = 2;
    source.gameRng = {{0x1234, 0x89ABCDEF}, {0xFEDC, 0x76543210}};
    std::array<uint8_t, coop::th12::kNetworkPacketSize> bytes{};
    CHECK(coop::th12::EncodeNetworkPacket(source, bytes));
    coop::th12::NetworkPacket decoded{};
    CHECK(coop::th12::DecodeNetworkPacket(bytes.data(), bytes.size(), decoded));
    CHECK(decoded.type == coop::th12::NetworkPacketType::kLobbyStart);
    CHECK(decoded.gameRng == source.gameRng);
    return true;
}

}  // namespace

int main() {
    if (!InputPacketRoundTrips() || !InputBundleRoundTrips() ||
        !CorruptionAndInvalidFieldsAreRejected() ||
        !StateHashesRoundTripWithoutTruncation() ||
        !LobbyStartRngStateRoundTripsWithoutTruncation()) {
        return 1;
    }
    std::cout << "network protocol tests passed\n";
    return 0;
}
