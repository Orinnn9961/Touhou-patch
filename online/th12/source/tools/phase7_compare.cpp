#include "determinism_trace.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

using coop::th12::DeterminismSample;

enum class ReadResult {
    kRecord,
    kEnd,
    kError,
};

bool ReadHeader(std::ifstream& stream) {
    std::array<uint8_t, coop::th12::kDeterminismTraceHeaderSize> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    return stream.gcount() == static_cast<std::streamsize>(bytes.size()) &&
           coop::th12::DecodeDeterminismTraceHeader(bytes);
}

ReadResult ReadSample(std::ifstream& stream, DeterminismSample& sample) {
    std::array<uint8_t, coop::th12::kDeterminismTraceRecordSize> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (stream.gcount() == 0 && stream.eof()) {
        return ReadResult::kEnd;
    }
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        !coop::th12::DecodeDeterminismSample(bytes, sample)) {
        return ReadResult::kError;
    }
    return ReadResult::kRecord;
}

void FieldDifference(const char* name, uint64_t left, uint64_t right) {
    if (left != right) {
        std::cout << "  " << name << ": " << left << " != " << right << '\n';
    }
}

void SignedFieldDifference(const char* name, int32_t left, int32_t right) {
    if (left != right) {
        std::cout << "  " << name << ": " << left << " != " << right << '\n';
    }
}

void PrintDifference(const DeterminismSample& left,
                     const DeterminismSample& right) {
    std::cout << "determinism mismatch at compared record, frame "
              << left.frame << " vs " << right.frame << '\n';
    FieldDifference("frame", left.frame, right.frame);
    FieldDifference("p1_input", left.player1Input, right.player1Input);
    FieldDifference("p2_input", left.player2Input, right.player2Input);
    SignedFieldDifference("p1_x", left.player1X, right.player1X);
    SignedFieldDifference("p1_y", left.player1Y, right.player1Y);
    SignedFieldDifference("p2_x", left.player2X, right.player2X);
    SignedFieldDifference("p2_y", left.player2Y, right.player2Y);
    FieldDifference("p1_focus", left.player1Focus, right.player1Focus);
    FieldDifference("p2_focus", left.player2Focus, right.player2Focus);
    FieldDifference("p1_state", left.player1State, right.player1State);
    FieldDifference("p1_state_frame", left.player1StateFrame,
                    right.player1StateFrame);
    FieldDifference("p1_invincibility", left.player1Invincibility,
                    right.player1Invincibility);
    FieldDifference("p2_state", left.player2State, right.player2State);
    FieldDifference("p2_state_frame", left.player2StateFrame,
                    right.player2StateFrame);
    FieldDifference("p2_invincibility", left.player2Invincibility,
                    right.player2Invincibility);
    FieldDifference("p2_recovery", left.player2RecoveryPhase,
                    right.player2RecoveryPhase);
    FieldDifference("power", left.power, right.power);
    FieldDifference("lives", left.lives, right.lives);
    FieldDifference("bombs", left.bombs, right.bombs);
    FieldDifference("life_fragments", left.lifeFragments,
                    right.lifeFragments);
    FieldDifference("bomb_fragments", left.bombFragments,
                    right.bombFragments);
    FieldDifference("rng_primary_seed", left.gameRng.primary.seed,
                    right.gameRng.primary.seed);
    FieldDifference("rng_primary_calls", left.gameRng.primary.calls,
                    right.gameRng.primary.calls);
    FieldDifference("rng_secondary_seed", left.gameRng.secondary.seed,
                    right.gameRng.secondary.seed);
    FieldDifference("rng_secondary_calls", left.gameRng.secondary.calls,
                    right.gameRng.secondary.calls);
    FieldDifference("p1_active_bullets", left.player1ActiveBullets,
                    right.player1ActiveBullets);
    FieldDifference("p2_active_bullets", left.player2ActiveBullets,
                    right.player2ActiveBullets);
    FieldDifference("p1_active_options", left.player1ActiveOptions,
                    right.player1ActiveOptions);
    FieldDifference("p2_active_options", left.player2ActiveOptions,
                    right.player2ActiveOptions);
    FieldDifference("p1_damage_areas", left.player1ActiveDamageAreas,
                    right.player1ActiveDamageAreas);
    FieldDifference("p2_damage_areas", left.player2ActiveDamageAreas,
                    right.player2ActiveDamageAreas);
    FieldDifference("p1_bomb_state", left.player1BombState,
                    right.player1BombState);
    FieldDifference("p2_bomb_state", left.player2BombState,
                    right.player2BombState);
    FieldDifference("p1_combat_hash", left.player1CombatHash,
                    right.player1CombatHash);
    FieldDifference("p2_combat_hash", left.player2CombatHash,
                    right.player2CombatHash);
    FieldDifference("collision_hash", left.collisionHash,
                    right.collisionHash);
    if (left.gameRngHash != right.gameRngHash) {
        std::cout << "  game_rng_hash: 0x" << std::hex << std::setw(16)
                  << std::setfill('0') << left.gameRngHash << " != 0x"
                  << std::setw(16) << right.gameRngHash << std::dec << '\n';
    }
    if (left.stateHash != right.stateHash) {
        std::cout << "  state_hash: 0x" << std::hex << std::setw(16)
                  << std::setfill('0') << left.stateHash << " != 0x"
                  << std::setw(16) << right.stateHash << std::dec << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    uint64_t frameLimit = 0;
    if (argc == 5 && std::string(argv[3]) == "--frames") {
        const std::string value(argv[4]);
        const auto parsed = std::from_chars(value.data(),
                                            value.data() + value.size(),
                                            frameLimit);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            frameLimit == 0) {
            std::cerr << "--frames must be a positive integer\n";
            return 2;
        }
    } else if (argc != 3) {
        std::cerr << "usage: phase7-compare <first-trace.bin> <second-trace.bin> "
                     "[--frames N]\n";
        return 2;
    }
    std::ifstream left(argv[1], std::ios::binary);
    std::ifstream right(argv[2], std::ios::binary);
    if (!left || !right || !ReadHeader(left) || !ReadHeader(right)) {
        std::cerr << "invalid or unsupported phase7 trace file\n";
        return 2;
    }

    uint64_t compared = 0;
    while (true) {
        DeterminismSample leftSample{};
        DeterminismSample rightSample{};
        const ReadResult leftResult = ReadSample(left, leftSample);
        const ReadResult rightResult = ReadSample(right, rightSample);
        if (leftResult == ReadResult::kError || rightResult == ReadResult::kError) {
            std::cerr << "corrupt or incomplete trace record after " << compared
                      << " frames\n";
            return 2;
        }
        if (leftResult == ReadResult::kEnd || rightResult == ReadResult::kEnd) {
            if (frameLimit != 0 && compared < frameLimit) {
                std::cout << "trace ended before requested frame count: compared "
                          << compared << " of " << frameLimit << " frames\n";
                return 1;
            }
            if (leftResult != rightResult) {
                std::cout << "trace length mismatch after " << compared << " frames\n";
                return 1;
            }
            std::cout << "determinism verified: " << compared
                      << " frames are identical\n";
            return 0;
        }
        ++compared;
        if (leftSample.stateHash != rightSample.stateHash) {
            PrintDifference(leftSample, rightSample);
            return 1;
        }
        if (frameLimit != 0 && compared == frameLimit) {
            std::cout << "determinism verified: first " << compared
                      << " frames are identical\n";
            return 0;
        }
    }
}
