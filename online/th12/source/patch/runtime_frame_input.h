#pragma once

#include "frame_input.h"

#include <cstdint>
#include <string>

namespace coop::th12 {

bool InitializeRuntimeFrameInput(const std::wstring& root,
                                 std::wstring& error) noexcept;
void ResetRuntimeFrameInputTimeline() noexcept;
bool BeginRuntimeFrameInput(uint32_t player1Mask, uint32_t player2Mask,
                            FrameInput& input) noexcept;
bool ConsumeRuntimeFrameInput(FrameInput& input) noexcept;
bool CaptureRuntimeFrameInput(uint32_t player1Mask, uint32_t player2Mask,
                              FrameInput& input) noexcept;
void SubmitRuntimeStateHash(uint32_t frame, uint64_t stateHash) noexcept;
bool RuntimeNetworkActive() noexcept;
bool RuntimeStateHashDue(uint32_t frame) noexcept;
bool CoordinateRuntimeAirframes(uint32_t localSelectedAirframe,
                                uint32_t& player1Airframe,
                                uint32_t& player2Airframe) noexcept;
void MarkRuntimeLobbyResourcesReady(bool ready) noexcept;
uint32_t RuntimeFrameNumber() noexcept;
uint32_t RuntimePhysicalPlayer1Input() noexcept;
LogicalPlayerSlot RuntimeLocalPlayerSlot() noexcept;
uint8_t RuntimeCoopRules() noexcept;

}  // namespace coop::th12
