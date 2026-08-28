#include "frame_input.h"

#include <cstdint>
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

bool ClockIsMonotonicAndSkipsZero() {
    coop::th12::FrameInputClock clock;
    CHECK(clock.Current() == 0);
    CHECK(clock.Next() == 1);
    CHECK(clock.Next() == 2);
    CHECK(clock.Current() == 2);
    clock.Reset(UINT32_MAX);
    CHECK(clock.Next() == UINT32_MAX);
    CHECK(clock.Next() == 1);
    return true;
}

bool BufferFindsUpdatesAndOverwritesByFrame() {
    coop::th12::FrameInputBuffer buffer;
    coop::th12::FrameInput found{};
    CHECK(!buffer.Push({0, 0, 0}));
    CHECK(!buffer.Push({1, 0x100, 0}));
    CHECK(buffer.Push({1, 0x10, 0x80}));
    CHECK(buffer.Size() == 1);
    CHECK(buffer.Find(1, found));
    CHECK(found.player1Mask == 0x10);
    CHECK(found.player2Mask == 0x80);

    CHECK(buffer.Push({1, 0x20, 0x40}));
    CHECK(buffer.Size() == 1);
    CHECK(buffer.Find(1, found));
    CHECK(found.player1Mask == 0x20);

    const uint32_t collidingFrame =
        1 + static_cast<uint32_t>(coop::th12::FrameInputBuffer::kCapacity);
    CHECK(buffer.Push({collidingFrame, 0x08, 0x10}));
    CHECK(buffer.Size() == 1);
    CHECK(!buffer.Find(1, found));
    CHECK(buffer.Find(collidingFrame, found));
    CHECK(buffer.Erase(collidingFrame));
    CHECK(buffer.Size() == 0);
    CHECK(!buffer.Erase(collidingFrame));
    return true;
}

bool ClearRemovesEveryStoredFrame() {
    coop::th12::FrameInputBuffer buffer;
    for (uint32_t frame = 1; frame <= 20; ++frame) {
        CHECK(buffer.Push({frame, frame & 0xFFU, 0}));
    }
    CHECK(buffer.Size() == 20);
    buffer.Clear();
    CHECK(buffer.Size() == 0);
    coop::th12::FrameInput found{};
    CHECK(!buffer.Find(10, found));
    return true;
}

bool LatchPublishesOnceAndConsumesOnce() {
    coop::th12::FrameInputLatch latch;
    coop::th12::FrameInput found{};
    CHECK(!latch.HasPending());
    CHECK(!latch.Publish({0, 0, 0}));
    CHECK(latch.Publish({1, 0x10, 0x80}));
    CHECK(latch.HasPending());
    CHECK(!latch.Publish({2, 0, 0}));
    CHECK(latch.Consume(found));
    CHECK(found.frame == 1);
    CHECK(found.player2Mask == 0x80);
    CHECK(!latch.HasPending());
    CHECK(!latch.Consume(found));
    latch.Clear();
    return true;
}

bool NetworkRolesKeepPhysicalDirectionsOnTheirLogicalPlayer() {
    using coop::th12::BuildRoleScopedFrameInput;
    using coop::th12::FrameInput;
    using coop::th12::LogicalPlayerSlot;
    using coop::th12::MergeRoleScopedFrameInputs;

    constexpr uint32_t kLeftShoot = 0x40 | 0x01;
    constexpr uint32_t kRightBombFocus = 0x80 | 0x02 | 0x08;
    FrameInput host{};
    FrameInput client{};
    FrameInput merged{};
    CHECK(BuildRoleScopedFrameInput(77, LogicalPlayerSlot::kPlayer1,
                                    kLeftShoot, host));
    CHECK(BuildRoleScopedFrameInput(77, LogicalPlayerSlot::kPlayer2,
                                    kRightBombFocus, client));
    CHECK(host.player1Mask == kLeftShoot && host.player2Mask == 0);
    CHECK(client.player1Mask == 0 && client.player2Mask == kRightBombFocus);
    CHECK(MergeRoleScopedFrameInputs(host, client, merged));
    CHECK(merged.player1Mask == kLeftShoot);
    CHECK(merged.player2Mask == kRightBombFocus);

    client.frame = 78;
    CHECK(!MergeRoleScopedFrameInputs(host, client, merged));
    CHECK(!BuildRoleScopedFrameInput(77,
                                     static_cast<LogicalPlayerSlot>(3),
                                     kLeftShoot, merged));
    return true;
}

}  // namespace

int main() {
    if (!ClockIsMonotonicAndSkipsZero() ||
        !BufferFindsUpdatesAndOverwritesByFrame() ||
        !ClearRemovesEveryStoredFrame() ||
        !LatchPublishesOnceAndConsumesOnce() ||
        !NetworkRolesKeepPhysicalDirectionsOnTheirLogicalPlayer()) {
        return 1;
    }
    std::cout << "frame input tests passed\n";
    return 0;
}
