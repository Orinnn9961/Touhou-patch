#pragma once

#include "frame_input.h"
#include "game_rng_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace coop::th12 {

enum class NetworkPacketType : uint8_t {
    kHello = 1,
    kHelloAck = 2,
    kInput = 3,
    kControl = 4,
    kStateHash = 5,
    kDisconnect = 6,
    kReject = 7,
    kLobbySelection = 8,
    kLobbyReady = 9,
    kLobbyStart = 10,
};

enum class NetworkControl : uint32_t {
    kRunning = 0,
    kPaused = 1,
};

struct NetworkPacket {
    NetworkPacketType type{NetworkPacketType::kHello};
    uint8_t flags{};
    uint32_t sessionId{};
    uint32_t frame{};
    uint32_t player1Mask{};
    uint32_t player2Mask{};
    uint32_t acknowledgedFrame{};
    uint32_t configWord{};
    uint64_t stateHash{};
    uint32_t controlValue{};
    GameRngState gameRng{};
    uint8_t inputCount{};
    std::array<uint8_t, 32> inputMasks{};
};

constexpr size_t kNetworkPacketSize = 92;
constexpr uint16_t kNetworkProtocolVersion = 7;

uint32_t PackNetworkConfig(uint8_t inputDelay,
                           uint8_t inputRedundancy) noexcept;

constexpr uint32_t kUnknownAirframe = 0xFFU;

uint32_t PackLobbyAirframes(uint8_t player1Airframe,
                            uint8_t player2Airframe) noexcept;
bool UnpackLobbyAirframes(uint32_t value, uint8_t& player1Airframe,
                          uint8_t& player2Airframe) noexcept;

bool EncodeNetworkPacket(const NetworkPacket& packet,
                         std::array<uint8_t, kNetworkPacketSize>& bytes) noexcept;
bool DecodeNetworkPacket(const uint8_t* bytes, size_t size,
                         NetworkPacket& packet) noexcept;

NetworkPacket MakeInputPacket(uint32_t sessionId, const FrameInput& input,
                              uint32_t acknowledgedFrame) noexcept;
NetworkPacket MakeStateHashPacket(uint32_t sessionId, uint32_t frame,
                                  uint64_t stateHash) noexcept;

}  // namespace coop::th12
