#include "script_input.h"

#include <iostream>

int main() {
    coop::th12::ScriptInputState state{
        0xFFFFFF00U, 0x12340000U, 0xABCD0000U, 0x98760000U};
    coop::th12::ApplyAuthoritativeScriptInput(state, 0x00000001U,
                                               0x00000000U);
    if (state.rawCurrent != 1 || state.rawPrevious != 0 ||
        state.processedCurrent != 1 || state.processedPrevious != 0) {
        std::cerr << "authoritative input projection failed\n";
        return 1;
    }
    coop::th12::ApplyAuthoritativeScriptInput(state, 0x00000302U,
                                               0x00000201U);
    if (state.rawCurrent != 2 || state.rawPrevious != 1 ||
        state.processedCurrent != 2 || state.processedPrevious != 1) {
        std::cerr << "upper input bits were not blocked\n";
        return 1;
    }
    std::cout << "script input tests passed\n";
    return 0;
}
