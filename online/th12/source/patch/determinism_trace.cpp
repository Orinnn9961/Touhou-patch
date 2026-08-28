#include "determinism_trace.h"

#include <algorithm>

namespace coop::th12 {
namespace {

constexpr std::array<uint8_t, 8> kMagic{
    'T', 'H', '1', '2', 'D', 'T', 'R', 'C',
};
constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void Put32(uint8_t* output, uint32_t value) noexcept {
    for (size_t i = 0; i < 4; ++i) {
        output[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

void Put64(uint8_t* output, uint64_t value) noexcept {
    for (size_t i = 0; i < 8; ++i) {
        output[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

uint32_t Get32(const uint8_t* input) noexcept {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(input[i]) << (i * 8);
    }
    return value;
}

uint64_t Get64(const uint8_t* input) noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(input[i]) << (i * 8);
    }
    return value;
}

uint64_t HashBytes(const uint8_t* bytes, size_t size) noexcept {
    uint64_t hash = kFnvOffset;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

}  // namespace

uint64_t HashGameRngState(const GameRngState& state) noexcept {
    uint8_t bytes[16]{};
    Put32(bytes + 0, state.primary.seed);
    Put32(bytes + 4, state.primary.calls);
    Put32(bytes + 8, state.secondary.seed);
    Put32(bytes + 12, state.secondary.calls);
    return HashBytes(bytes, sizeof(bytes));
}

uint64_t HashDeterminismSample(const DeterminismSample& sample) noexcept {
    const auto bytes = EncodeDeterminismSample(sample);
    // collisionHash is a cumulative diagnostic counter. Hook call counts can
    // differ between render/scheduler paths without changing simulation state,
    // so neither it nor the final stateHash participates in network checking.
    return HashBytes(bytes.data(), 184);
}

std::array<uint8_t, kDeterminismTraceHeaderSize>
EncodeDeterminismTraceHeader() noexcept {
    std::array<uint8_t, kDeterminismTraceHeaderSize> bytes{};
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    Put32(bytes.data() + 8, kDeterminismTraceVersion);
    Put32(bytes.data() + 12, static_cast<uint32_t>(kDeterminismTraceRecordSize));
    return bytes;
}

bool DecodeDeterminismTraceHeader(
    const std::array<uint8_t, kDeterminismTraceHeaderSize>& bytes) noexcept {
    return std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) &&
           Get32(bytes.data() + 8) == kDeterminismTraceVersion &&
           Get32(bytes.data() + 12) == kDeterminismTraceRecordSize;
}

std::array<uint8_t, kDeterminismTraceRecordSize>
EncodeDeterminismSample(const DeterminismSample& sample) noexcept {
    std::array<uint8_t, kDeterminismTraceRecordSize> bytes{};
    Put32(bytes.data() + 0, sample.frame);
    Put32(bytes.data() + 4, sample.player1Input);
    Put32(bytes.data() + 8, sample.player2Input);
    Put32(bytes.data() + 12, static_cast<uint32_t>(sample.player1X));
    Put32(bytes.data() + 16, static_cast<uint32_t>(sample.player1Y));
    Put32(bytes.data() + 20, static_cast<uint32_t>(sample.player2X));
    Put32(bytes.data() + 24, static_cast<uint32_t>(sample.player2Y));
    Put32(bytes.data() + 28, sample.player1Focus);
    Put32(bytes.data() + 32, sample.player2Focus);
    Put32(bytes.data() + 36, sample.player1State);
    Put32(bytes.data() + 40, sample.player1StateFrame);
    Put32(bytes.data() + 44, sample.player1Invincibility);
    Put32(bytes.data() + 48, sample.player2State);
    Put32(bytes.data() + 52, sample.player2StateFrame);
    Put32(bytes.data() + 56, sample.player2Invincibility);
    Put32(bytes.data() + 60, sample.player2RecoveryPhase);
    Put32(bytes.data() + 64, sample.power);
    Put32(bytes.data() + 68, sample.lives);
    Put32(bytes.data() + 72, sample.bombs);
    Put32(bytes.data() + 76, sample.lifeFragments);
    Put32(bytes.data() + 80, sample.bombFragments);
    Put32(bytes.data() + 84, sample.ufo0);
    Put32(bytes.data() + 88, sample.ufo1);
    Put32(bytes.data() + 92, sample.ufo2);
    Put32(bytes.data() + 96, sample.ufoState);
    Put32(bytes.data() + 100, sample.ufoFlags);
    Put32(bytes.data() + 104, sample.score);
    Put32(bytes.data() + 108, sample.pointValue);
    Put32(bytes.data() + 112, sample.gameRng.primary.seed);
    Put32(bytes.data() + 116, sample.gameRng.primary.calls);
    Put32(bytes.data() + 120, sample.gameRng.secondary.seed);
    Put32(bytes.data() + 124, sample.gameRng.secondary.calls);
    Put64(bytes.data() + 128, sample.gameRngHash);
    Put64(bytes.data() + 136, sample.player1CombatHash);
    Put64(bytes.data() + 144, sample.player2CombatHash);
    Put32(bytes.data() + 152, sample.player1ActiveBullets);
    Put32(bytes.data() + 156, sample.player2ActiveBullets);
    Put32(bytes.data() + 160, sample.player1ActiveOptions);
    Put32(bytes.data() + 164, sample.player2ActiveOptions);
    Put32(bytes.data() + 168, sample.player1ActiveDamageAreas);
    Put32(bytes.data() + 172, sample.player2ActiveDamageAreas);
    Put32(bytes.data() + 176, sample.player1BombState);
    Put32(bytes.data() + 180, sample.player2BombState);
    Put64(bytes.data() + 184, sample.collisionHash);
    Put64(bytes.data() + 192, sample.stateHash);
    return bytes;
}

bool DecodeDeterminismSample(
    const std::array<uint8_t, kDeterminismTraceRecordSize>& bytes,
    DeterminismSample& sample) noexcept {
    sample.frame = Get32(bytes.data() + 0);
    sample.player1Input = Get32(bytes.data() + 4);
    sample.player2Input = Get32(bytes.data() + 8);
    sample.player1X = static_cast<int32_t>(Get32(bytes.data() + 12));
    sample.player1Y = static_cast<int32_t>(Get32(bytes.data() + 16));
    sample.player2X = static_cast<int32_t>(Get32(bytes.data() + 20));
    sample.player2Y = static_cast<int32_t>(Get32(bytes.data() + 24));
    sample.player1Focus = Get32(bytes.data() + 28);
    sample.player2Focus = Get32(bytes.data() + 32);
    sample.player1State = Get32(bytes.data() + 36);
    sample.player1StateFrame = Get32(bytes.data() + 40);
    sample.player1Invincibility = Get32(bytes.data() + 44);
    sample.player2State = Get32(bytes.data() + 48);
    sample.player2StateFrame = Get32(bytes.data() + 52);
    sample.player2Invincibility = Get32(bytes.data() + 56);
    sample.player2RecoveryPhase = Get32(bytes.data() + 60);
    sample.power = Get32(bytes.data() + 64);
    sample.lives = Get32(bytes.data() + 68);
    sample.bombs = Get32(bytes.data() + 72);
    sample.lifeFragments = Get32(bytes.data() + 76);
    sample.bombFragments = Get32(bytes.data() + 80);
    sample.ufo0 = Get32(bytes.data() + 84);
    sample.ufo1 = Get32(bytes.data() + 88);
    sample.ufo2 = Get32(bytes.data() + 92);
    sample.ufoState = Get32(bytes.data() + 96);
    sample.ufoFlags = Get32(bytes.data() + 100);
    sample.score = Get32(bytes.data() + 104);
    sample.pointValue = Get32(bytes.data() + 108);
    sample.gameRng.primary.seed =
        static_cast<uint16_t>(Get32(bytes.data() + 112));
    sample.gameRng.primary.calls = Get32(bytes.data() + 116);
    sample.gameRng.secondary.seed =
        static_cast<uint16_t>(Get32(bytes.data() + 120));
    sample.gameRng.secondary.calls = Get32(bytes.data() + 124);
    sample.gameRngHash = Get64(bytes.data() + 128);
    sample.player1CombatHash = Get64(bytes.data() + 136);
    sample.player2CombatHash = Get64(bytes.data() + 144);
    sample.player1ActiveBullets = Get32(bytes.data() + 152);
    sample.player2ActiveBullets = Get32(bytes.data() + 156);
    sample.player1ActiveOptions = Get32(bytes.data() + 160);
    sample.player2ActiveOptions = Get32(bytes.data() + 164);
    sample.player1ActiveDamageAreas = Get32(bytes.data() + 168);
    sample.player2ActiveDamageAreas = Get32(bytes.data() + 172);
    sample.player1BombState = Get32(bytes.data() + 176);
    sample.player2BombState = Get32(bytes.data() + 180);
    sample.collisionHash = Get64(bytes.data() + 184);
    sample.stateHash = Get64(bytes.data() + 192);
    return sample.gameRngHash == HashGameRngState(sample.gameRng) &&
           sample.stateHash == HashDeterminismSample(sample);
}

}  // namespace coop::th12
