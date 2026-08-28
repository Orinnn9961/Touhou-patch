#include "lockstep_timeline.h"

#include <iostream>

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "check failed at line " << __LINE__ << "\n";       \
            return false;                                                    \
        }                                                                    \
    } while (false)

bool FixedDelayAndWarmupAreDeterministic() {
    coop::th12::LockstepTimeline host;
    CHECK(host.Configure(coop::th12::LogicalPlayerSlot::kPlayer1, 3));
    CHECK(host.SimulationFrame() == 1);
    CHECK(host.CaptureFrame() == 4);

    coop::th12::FrameInput capture{};
    CHECK(host.BuildLocalCapture(0x51, capture));
    CHECK(capture.frame == 4 && capture.player1Mask == 0x51);

    coop::th12::FrameInput local{};
    coop::th12::FrameInput remote{};
    coop::th12::FrameInput merged{};
    CHECK(host.BuildWarmupInputs(local, remote));
    CHECK(host.Resolve(local, remote, merged));
    CHECK(merged.frame == 1 && merged.player1Mask == 0 &&
          merged.player2Mask == 0);
    CHECK(host.SimulationFrame() == 2 && host.CaptureFrame() == 5);
    return true;
}

bool RolesMergeIntoStableSlots() {
    coop::th12::LockstepTimeline client;
    CHECK(client.Configure(coop::th12::LogicalPlayerSlot::kPlayer2, 0));
    coop::th12::FrameInput local{};
    CHECK(client.BuildLocalCapture(0x89, local));
    const coop::th12::FrameInput remote{1, 0x41, 0};
    coop::th12::FrameInput merged{};
    CHECK(client.Resolve(local, remote, merged));
    CHECK(merged.player1Mask == 0x41 && merged.player2Mask == 0x89);
    return true;
}

}  // namespace

int main() {
    return FixedDelayAndWarmupAreDeterministic() && RolesMergeIntoStableSlots()
        ? 0
        : 1;
}
