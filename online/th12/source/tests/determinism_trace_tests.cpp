#include "determinism_trace.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "CHECK failed at line " << __LINE__ << ": "          \
                      << #condition << '\n';                                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

}  // namespace

int main() {
    using namespace coop::th12;
    CHECK(DecodeDeterminismTraceHeader(EncodeDeterminismTraceHeader()));

    DeterminismSample sample{};
    sample.frame = 123;
    sample.player1Input = 0x11;
    sample.player2Input = 0x24;
    sample.player1X = -23552;
    sample.player1Y = 4096;
    sample.player2X = 23552;
    sample.player2Y = 55296;
    sample.player1Focus = 1;
    sample.player2Focus = 0;
    sample.player1State = 1;
    sample.player2State = 4;
    sample.power = 150;
    sample.lives = 2;
    sample.bombs = 3;
    sample.gameRng = {{0x1234, 5678}, {0x9ABC, 4321}};
    sample.gameRngHash = HashGameRngState(sample.gameRng);
    sample.stateHash = HashDeterminismSample(sample);

    DeterminismSample decoded{};
    CHECK(DecodeDeterminismSample(EncodeDeterminismSample(sample), decoded));
    CHECK(decoded.frame == sample.frame);
    CHECK(decoded.player1X == sample.player1X);
    CHECK(decoded.player2Y == sample.player2Y);
    CHECK(decoded.gameRng == sample.gameRng);
    CHECK(decoded.gameRngHash == sample.gameRngHash);
    CHECK(decoded.stateHash == sample.stateHash);

    auto corrupt = EncodeDeterminismSample(sample);
    corrupt[20] ^= 1;
    CHECK(!DecodeDeterminismSample(corrupt, decoded));

    DeterminismSample changed = sample;
    ++changed.player2X;
    changed.stateHash = HashDeterminismSample(changed);
    CHECK(changed.stateHash != sample.stateHash);
    DeterminismSample extended = sample;
    ++extended.gameRng.primary.calls;
    extended.gameRngHash = HashGameRngState(extended.gameRng);
    extended.power += 50;
    extended.player2CombatHash = 0x123456789ABCDEF0ULL;
    extended.stateHash = HashDeterminismSample(extended);
    CHECK(extended.stateHash != sample.stateHash);
    DeterminismSample diagnosticOnly = sample;
    diagnosticOnly.collisionHash = 0xAABBCCDDEEFF0011ULL;
    CHECK(HashDeterminismSample(diagnosticOnly) == sample.stateHash);
    CHECK(HashGameRngState(sample.gameRng) ==
          HashGameRngState(sample.gameRng));
    GameRngState changedRng = sample.gameRng;
    ++changedRng.primary.calls;
    CHECK(HashGameRngState(sample.gameRng) != HashGameRngState(changedRng));

    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "determinism trace tests passed\n";
    return EXIT_SUCCESS;
}
