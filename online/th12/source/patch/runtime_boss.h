#pragma once

#include <cstdint>
#include <string>

namespace coop::th12 {

bool InitializeRuntimeBosses(const std::wstring& root,
                             std::wstring& error) noexcept;
uint32_t RuntimeBossLifeScaleCount() noexcept;
uint64_t RuntimeBossDamageScaleHash() noexcept;

}  // namespace coop::th12
