#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace coop::th12 {

enum class LogicalPlayerSlot : uint8_t {
    kPlayer1 = 1,
    kPlayer2 = 2,
};

struct FrameInput {
    uint32_t frame{};
    uint32_t player1Mask{};
    uint32_t player2Mask{};

    bool IsValid() const noexcept;
};

bool BuildRoleScopedFrameInput(uint32_t frame, LogicalPlayerSlot localPlayer,
                               uint32_t localActionMask,
                               FrameInput& input) noexcept;
bool MergeRoleScopedFrameInputs(const FrameInput& player1Input,
                                const FrameInput& player2Input,
                                FrameInput& input) noexcept;

class FrameInputClock {
public:
    void Reset(uint32_t firstFrame = 1) noexcept;
    uint32_t Next() noexcept;
    uint32_t Current() const noexcept;

private:
    uint32_t nextFrame_{1};
};

class FrameInputBuffer {
public:
    static constexpr size_t kCapacity = 128;

    bool Push(const FrameInput& input) noexcept;
    bool Find(uint32_t frame, FrameInput& input) const noexcept;
    bool Erase(uint32_t frame) noexcept;
    void Clear() noexcept;
    size_t Size() const noexcept;

private:
    size_t Index(uint32_t frame) const noexcept;

    std::array<FrameInput, kCapacity> entries_{};
    std::array<bool, kCapacity> valid_{};
    size_t size_{};
};

class FrameInputLatch {
public:
    bool Publish(const FrameInput& input) noexcept;
    bool Consume(FrameInput& input) noexcept;
    bool HasPending() const noexcept;
    void Clear() noexcept;

private:
    FrameInput pending_{};
    bool valid_{};
};

}  // namespace coop::th12
