#pragma once

#include "shared_resource.h"

#include <cstdint>
#include <string>

namespace coop::th12 {

bool InitializeRuntimeResourceTransactions(const std::wstring& root,
                                           std::wstring& error) noexcept;
bool RuntimeResourceTransactionsEnabled() noexcept;
bool RuntimeResourceFrameActive() noexcept;
void BeginRuntimeResourceFrame() noexcept;
void ExposeRuntimeResourceProjection() noexcept;
void CaptureRuntimeNativeResourceChanges(uint8_t player) noexcept;
void CommitRuntimeResourceFrame() noexcept;
void AbortRuntimeResourceFrame() noexcept;
void QueueRuntimeResourceDelta(SharedResource resource, int32_t delta,
                               uint8_t player) noexcept;
int32_t RuntimeProjectedResource(SharedResource resource) noexcept;

}  // namespace coop::th12
