#include "network_protocol.h"

#include <cstring>

namespace coop::th12 {
namespace {

constexpr uint32_t kNetworkMagic = 0x4B4C4F43;  // "COLK" in little-endian.
constexpr size_t kHeaderSize = 88;

void Write16(std::array<uint8_t, kNetworkPacketSize>& bytes, size_t offset,
             uint16_t value) noexcept {
    bytes[offset] = static_cast<uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void Write32(std::array<uint8_t, kNetworkPacketSize>& bytes, size_t offset,
             uint32_t value) noexcept {
    for (size_t i = 0; i < sizeof(value); ++i) {
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
    }
}

uint16_t Read16(const uint8_t* bytes, size_t offset) noexcept {
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(bytes[offset + 1]) << 8U;
}

uint32_t Read32(const uint8_t* bytes, size_t offset) noexcept {
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<uint32_t>(bytes[offset + i]) << (i * 8U);
    }
    return value;
}

void Write64(std::array<uint8_t, kNetworkPacketSize>& bytes, size_t offset,
             uint64_t value) noexcept {
    for (size_t i = 0; i < sizeof(value); ++i) {
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
    }
}

uint64_t Read64(const uint8_t* bytes, size_t offset) noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8U);
    }
    return value;
}

uint32_t Checksum(const uint8_t* bytes, size_t size) noexcept {
    uint32_t result = 2166136261U;
    for (size_t i = 0; i < size; ++i) {
        result ^= bytes[i];
        result *= 16777619U;
    }
    return result;
}

bool IsKnownType(uint8_t value) noexcept {
    return value >= static_cast<uint8_t>(NetworkPacketType::kHello) &&
           value <= static_cast<uint8_t>(NetworkPacketType::kLobbyStart);
}

}  // namespace

uint32_t PackNetworkConfig(uint8_t inputDelay,
                           uint8_t inputRedundancy) noexcept {
    return static_cast<uint32_t>(inputDelay) |
           static_cast<uint32_t>(inputRedundancy) << 8U;
}

uint32_t PackLobbyAirframes(uint8_t player1Airframe,
                            uint8_t player2Airframe) noexcept {
    return static_cast<uint32_t>(player1Airframe) |
           static_cast<uint32_t>(player2Airframe) << 8U;
}

bool UnpackLobbyAirframes(uint32_t value, uint8_t& player1Airframe,
                          uint8_t& player2Airframe) noexcept {
    player1Airframe = static_cast<uint8_t>(value & 0xFFU);
    player2Airframe = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    return (player1Airframe == kUnknownAirframe || player1Airframe < 6) &&
           (player2Airframe == kUnknownAirframe || player2Airframe < 6);
}

bool EncodeNetworkPacket(const NetworkPacket& packet,
                         std::array<uint8_t, kNetworkPacketSize>& bytes) noexcept {
    if (packet.sessionId == 0 || !IsKnownType(static_cast<uint8_t>(packet.type))) {
        return false;
    }
    if ((packet.player1Mask & ~0xFFU) != 0 ||
        (packet.player2Mask & ~0xFFU) != 0) {
        return false;
    }
    bytes.fill(0);
    Write32(bytes, 0, kNetworkMagic);
    Write16(bytes, 4, kNetworkProtocolVersion);
    bytes[6] = static_cast<uint8_t>(packet.type);
    bytes[7] = packet.flags;
    Write32(bytes, 8, packet.sessionId);
    Write32(bytes, 12, packet.frame);
    Write32(bytes, 16, packet.player1Mask);
    Write32(bytes, 20, packet.player2Mask);
    Write32(bytes, 24, packet.acknowledgedFrame);
    Write32(bytes, 28, packet.configWord);
    if (packet.type == NetworkPacketType::kInput) {
        if (packet.frame == 0 || packet.inputCount == 0 ||
            packet.inputCount > packet.inputMasks.size() ||
            packet.inputCount > packet.frame) {
            return false;
        }
        for (size_t i = 0; i < packet.inputCount; ++i) {
            bytes[56 + i] = packet.inputMasks[i];
        }
        bytes[48] = packet.inputCount;
    } else {
        Write64(bytes, 32, packet.stateHash);
        Write32(bytes, 40, packet.controlValue);
        Write16(bytes, 44, packet.gameRng.primary.seed);
        Write16(bytes, 46, packet.gameRng.secondary.seed);
        Write32(bytes, 48, packet.gameRng.primary.calls);
        Write32(bytes, 52, packet.gameRng.secondary.calls);
    }
    Write32(bytes, kHeaderSize, Checksum(bytes.data(), kHeaderSize));
    return true;
}

bool DecodeNetworkPacket(const uint8_t* bytes, size_t size,
                         NetworkPacket& packet) noexcept {
    if (bytes == nullptr || size != kNetworkPacketSize ||
        Read32(bytes, 0) != kNetworkMagic ||
        Read16(bytes, 4) != kNetworkProtocolVersion ||
        !IsKnownType(bytes[6]) ||
        Read32(bytes, kHeaderSize) != Checksum(bytes, kHeaderSize)) {
        return false;
    }
    packet = {};
    packet.type = static_cast<NetworkPacketType>(bytes[6]);
    packet.flags = bytes[7];
    packet.sessionId = Read32(bytes, 8);
    packet.frame = Read32(bytes, 12);
    packet.player1Mask = Read32(bytes, 16);
    packet.player2Mask = Read32(bytes, 20);
    packet.acknowledgedFrame = Read32(bytes, 24);
    packet.configWord = Read32(bytes, 28);
    if (packet.type == NetworkPacketType::kInput) {
        packet.inputCount = bytes[48];
        if (packet.frame == 0 || packet.inputCount == 0 ||
            packet.inputCount > packet.inputMasks.size() ||
            packet.inputCount > packet.frame) {
            return false;
        }
        for (size_t i = 0; i < packet.inputCount; ++i) {
            packet.inputMasks[i] = bytes[56 + i];
        }
    } else {
        packet.stateHash = Read64(bytes, 32);
        packet.controlValue = Read32(bytes, 40);
        packet.gameRng.primary.seed = Read16(bytes, 44);
        packet.gameRng.secondary.seed = Read16(bytes, 46);
        packet.gameRng.primary.calls = Read32(bytes, 48);
        packet.gameRng.secondary.calls = Read32(bytes, 52);
    }
    return packet.sessionId != 0 && (packet.player1Mask & ~0xFFU) == 0 &&
           (packet.player2Mask & ~0xFFU) == 0;
}

NetworkPacket MakeInputPacket(uint32_t sessionId, const FrameInput& input,
                              uint32_t acknowledgedFrame) noexcept {
    NetworkPacket packet{};
    packet.type = NetworkPacketType::kInput;
    packet.sessionId = sessionId;
    packet.frame = input.frame;
    packet.player1Mask = input.player1Mask;
    packet.player2Mask = input.player2Mask;
    packet.acknowledgedFrame = acknowledgedFrame;
    packet.inputCount = 1;
    packet.inputMasks[0] = static_cast<uint8_t>(
        input.player1Mask != 0 ? input.player1Mask : input.player2Mask);
    return packet;
}

NetworkPacket MakeStateHashPacket(uint32_t sessionId, uint32_t frame,
                                  uint64_t stateHash) noexcept {
    return {NetworkPacketType::kStateHash, 0, sessionId, frame,
            0, 0, 0, 0, stateHash, 0};
}

}  // namespace coop::th12
