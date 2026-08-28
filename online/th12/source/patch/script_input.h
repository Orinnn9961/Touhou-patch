#pragma once

#include <cstdint>

namespace coop::th12 {

// Dialogue and menus can read the raw keyboard snapshot while PlayerUpdate
// reads the processed snapshot. Network play must project the same P1 input
// into both representations.
struct ScriptInputState {
    uint32_t rawCurrent{};
    uint32_t rawPrevious{};
    uint32_t processedCurrent{};
    uint32_t processedPrevious{};
};

constexpr uint32_t kNetworkActionMask = 0xFFU;

void ApplyAuthoritativeScriptInput(ScriptInputState& state,
                                   uint32_t current,
                                   uint32_t previous) noexcept;

}  // namespace coop::th12
