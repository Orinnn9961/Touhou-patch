#pragma once

#include "frame_input.h"

#include <cstdint>

namespace coop::th12 {

class LockstepTimeline {
public:
    bool Configure(LogicalPlayerSlot localPlayer, uint8_t inputDelay) noexcept;
    void Reset() noexcept;

    uint32_t SimulationFrame() const noexcept;
    uint32_t CaptureFrame() const noexcept;
    bool BuildLocalCapture(uint32_t localMask, FrameInput& input) const noexcept;
    bool BuildWarmupInputs(FrameInput& local, FrameInput& remote) const noexcept;
    bool Resolve(const FrameInput& local, const FrameInput& remote,
                 FrameInput& merged) noexcept;

private:
    LogicalPlayerSlot localPlayer_{LogicalPlayerSlot::kPlayer1};
    uint8_t inputDelay_{};
    uint32_t simulationFrame_{1};
    bool configured_{};
};

}  // namespace coop::th12
