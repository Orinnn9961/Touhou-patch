#pragma once

#include "determinism_trace.h"
#include "frame_input.h"

#include <string>

namespace coop::th12 {

bool InitializeRuntimeDeterminism(const std::wstring& root,
                                  std::wstring& error) noexcept;
void ResetRuntimeDeterminismTrace() noexcept;
void FinishRuntimeDeterminismTrace() noexcept;
bool CaptureRuntimeDeterminism(const FrameInput& input, void* player1,
                               void* player2) noexcept;
void QueueRuntimeEndOfFrameDeterminism(const FrameInput& input, void* player1,
                                       void* player2) noexcept;
void CompleteRuntimeEndOfFrameDeterminism() noexcept;
uint32_t RuntimeLastDeterminismFrame() noexcept;
uint64_t RuntimeLastDeterminismHash() noexcept;
bool RuntimeLastDeterminismSample(DeterminismSample& sample) noexcept;
bool RuntimeDeterminismMovementOnly() noexcept;
GameRngState ReadRuntimeGameRngState() noexcept;
void WriteRuntimeGameRngState(const GameRngState& state) noexcept;

}  // namespace coop::th12
