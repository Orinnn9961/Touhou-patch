#pragma once

#include "game_rng_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace coop::th12 {

constexpr uint32_t kDeterminismTraceVersion = 6;
constexpr size_t kDeterminismTraceHeaderSize = 16;
constexpr size_t kDeterminismTraceRecordSize = 200;

struct DeterminismSample {
    uint32_t frame{};
    uint32_t player1Input{};
    uint32_t player2Input{};
    int32_t player1X{};
    int32_t player1Y{};
    int32_t player2X{};
    int32_t player2Y{};
    uint32_t player1Focus{};
    uint32_t player2Focus{};
    uint32_t player1State{};
    uint32_t player1StateFrame{};
    uint32_t player1Invincibility{};
    uint32_t player2State{};
    uint32_t player2StateFrame{};
    uint32_t player2Invincibility{};
    uint32_t player2RecoveryPhase{};
    uint32_t power{};
    uint32_t lives{};
    uint32_t bombs{};
    uint32_t lifeFragments{};
    uint32_t bombFragments{};
    uint32_t ufo0{};
    uint32_t ufo1{};
    uint32_t ufo2{};
    uint32_t ufoState{};
    uint32_t ufoFlags{};
    uint32_t score{};
    uint32_t pointValue{};
    GameRngState gameRng{};
    uint64_t gameRngHash{};
    uint64_t player1CombatHash{};
    uint64_t player2CombatHash{};
    uint32_t player1ActiveBullets{};
    uint32_t player2ActiveBullets{};
    uint32_t player1ActiveOptions{};
    uint32_t player2ActiveOptions{};
    uint32_t player1ActiveDamageAreas{};
    uint32_t player2ActiveDamageAreas{};
    uint32_t player1BombState{};
    uint32_t player2BombState{};
    uint64_t collisionHash{};
    uint64_t stateHash{};
};

uint64_t HashGameRngState(const GameRngState& state) noexcept;
uint64_t HashDeterminismSample(const DeterminismSample& sample) noexcept;

std::array<uint8_t, kDeterminismTraceHeaderSize>
EncodeDeterminismTraceHeader() noexcept;
bool DecodeDeterminismTraceHeader(
    const std::array<uint8_t, kDeterminismTraceHeaderSize>& bytes) noexcept;

std::array<uint8_t, kDeterminismTraceRecordSize>
EncodeDeterminismSample(const DeterminismSample& sample) noexcept;
bool DecodeDeterminismSample(
    const std::array<uint8_t, kDeterminismTraceRecordSize>& bytes,
    DeterminismSample& sample) noexcept;

}  // namespace coop::th12
