#include "frame_input.h"

namespace coop::th12 {

bool FrameInput::IsValid() const noexcept {
    return frame != 0 && (player1Mask & ~0xFFU) == 0 &&
           (player2Mask & ~0xFFU) == 0;
}

bool BuildRoleScopedFrameInput(uint32_t frame, LogicalPlayerSlot localPlayer,
                               uint32_t localActionMask,
                               FrameInput& input) noexcept {
    if (frame == 0 || (localActionMask & ~0xFFU) != 0) {
        return false;
    }
    switch (localPlayer) {
    case LogicalPlayerSlot::kPlayer1:
        input = {frame, localActionMask, 0};
        return true;
    case LogicalPlayerSlot::kPlayer2:
        input = {frame, 0, localActionMask};
        return true;
    default:
        return false;
    }
}

bool MergeRoleScopedFrameInputs(const FrameInput& player1Input,
                                const FrameInput& player2Input,
                                FrameInput& input) noexcept {
    if (!player1Input.IsValid() || !player2Input.IsValid() ||
        player1Input.frame != player2Input.frame ||
        player1Input.player2Mask != 0 || player2Input.player1Mask != 0) {
        return false;
    }
    input = {player1Input.frame, player1Input.player1Mask,
             player2Input.player2Mask};
    return true;
}

void FrameInputClock::Reset(uint32_t firstFrame) noexcept {
    nextFrame_ = firstFrame == 0 ? 1 : firstFrame;
}

uint32_t FrameInputClock::Next() noexcept {
    const uint32_t frame = nextFrame_;
    ++nextFrame_;
    if (nextFrame_ == 0) {
        nextFrame_ = 1;
    }
    return frame;
}

uint32_t FrameInputClock::Current() const noexcept {
    return nextFrame_ == 1 ? 0 : nextFrame_ - 1;
}

bool FrameInputBuffer::Push(const FrameInput& input) noexcept {
    if (!input.IsValid()) {
        return false;
    }
    const size_t index = Index(input.frame);
    if (!valid_[index]) {
        ++size_;
    }
    entries_[index] = input;
    valid_[index] = true;
    if (size_ > kCapacity) {
        size_ = kCapacity;
    }
    return true;
}

bool FrameInputBuffer::Find(uint32_t frame, FrameInput& input) const noexcept {
    if (frame == 0) {
        return false;
    }
    const size_t index = Index(frame);
    if (!valid_[index] || entries_[index].frame != frame) {
        return false;
    }
    input = entries_[index];
    return true;
}

bool FrameInputBuffer::Erase(uint32_t frame) noexcept {
    if (frame == 0) {
        return false;
    }
    const size_t index = Index(frame);
    if (!valid_[index] || entries_[index].frame != frame) {
        return false;
    }
    valid_[index] = false;
    entries_[index] = {};
    --size_;
    return true;
}

void FrameInputBuffer::Clear() noexcept {
    valid_.fill(false);
    entries_.fill({});
    size_ = 0;
}

size_t FrameInputBuffer::Size() const noexcept {
    return size_;
}

size_t FrameInputBuffer::Index(uint32_t frame) const noexcept {
    return static_cast<size_t>(frame % static_cast<uint32_t>(kCapacity));
}

bool FrameInputLatch::Publish(const FrameInput& input) noexcept {
    if (valid_ || !input.IsValid()) {
        return false;
    }
    pending_ = input;
    valid_ = true;
    return true;
}

bool FrameInputLatch::Consume(FrameInput& input) noexcept {
    if (!valid_) {
        return false;
    }
    input = pending_;
    pending_ = {};
    valid_ = false;
    return true;
}

bool FrameInputLatch::HasPending() const noexcept {
    return valid_;
}

void FrameInputLatch::Clear() noexcept {
    pending_ = {};
    valid_ = false;
}

}  // namespace coop::th12
