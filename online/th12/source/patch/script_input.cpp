#include "script_input.h"

namespace coop::th12 {

void ApplyAuthoritativeScriptInput(ScriptInputState& state,
                                   uint32_t current,
                                   uint32_t previous) noexcept {
    // Network packets intentionally contain only gameplay actions. Clearing
    // the upper bits also blocks client-only menu/replay flags from advancing
    // a script between lockstep frames.
    state.rawCurrent = current & kNetworkActionMask;
    state.rawPrevious = previous & kNetworkActionMask;
    state.processedCurrent = state.rawCurrent;
    state.processedPrevious = state.rawPrevious;
}

}  // namespace coop::th12
