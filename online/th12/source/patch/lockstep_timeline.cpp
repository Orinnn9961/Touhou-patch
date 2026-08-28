#include "lockstep_timeline.h"

namespace coop::th12 {

bool LockstepTimeline::Configure(LogicalPlayerSlot localPlayer,
                                 uint8_t inputDelay) noexcept {
    if ((localPlayer != LogicalPlayerSlot::kPlayer1 &&
         localPlayer != LogicalPlayerSlot::kPlayer2) ||
        inputDelay > 12) {
        return false;
    }
    localPlayer_ = localPlayer;
    inputDelay_ = inputDelay;
    configured_ = true;
    Reset();
    return true;
}

void LockstepTimeline::Reset() noexcept {
    simulationFrame_ = 1;
}

uint32_t LockstepTimeline::SimulationFrame() const noexcept {
    return configured_ ? simulationFrame_ : 0;
}

uint32_t LockstepTimeline::CaptureFrame() const noexcept {
    return configured_ ? simulationFrame_ + inputDelay_ : 0;
}

bool LockstepTimeline::BuildLocalCapture(uint32_t localMask,
                                         FrameInput& input) const noexcept {
    return configured_ &&
           BuildRoleScopedFrameInput(CaptureFrame(), localPlayer_,
                                     localMask & 0xFFU, input);
}

bool LockstepTimeline::BuildWarmupInputs(FrameInput& local,
                                         FrameInput& remote) const noexcept {
    if (!configured_ || simulationFrame_ > inputDelay_) {
        return false;
    }
    const LogicalPlayerSlot remotePlayer =
        localPlayer_ == LogicalPlayerSlot::kPlayer1
            ? LogicalPlayerSlot::kPlayer2
            : LogicalPlayerSlot::kPlayer1;
    return BuildRoleScopedFrameInput(simulationFrame_, localPlayer_, 0, local) &&
           BuildRoleScopedFrameInput(simulationFrame_, remotePlayer, 0, remote);
}

bool LockstepTimeline::Resolve(const FrameInput& local,
                               const FrameInput& remote,
                               FrameInput& merged) noexcept {
    if (!configured_ || local.frame != simulationFrame_ ||
        remote.frame != simulationFrame_) {
        return false;
    }
    const bool result = localPlayer_ == LogicalPlayerSlot::kPlayer1
        ? MergeRoleScopedFrameInputs(local, remote, merged)
        : MergeRoleScopedFrameInputs(remote, local, merged);
    if (!result) {
        return false;
    }
    ++simulationFrame_;
    if (simulationFrame_ == 0) {
        simulationFrame_ = 1;
    }
    return true;
}

}  // namespace coop::th12
