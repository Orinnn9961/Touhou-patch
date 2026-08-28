#include "player2_input.h"

#include <iostream>

namespace {

static_assert(coop::th12::kInputShoot == 0x01);
static_assert(coop::th12::kInputBomb == 0x02);
static_assert(coop::th12::kInputFocus == 0x08);
static_assert(coop::th12::kInputUp == 0x10);
static_assert(coop::th12::kInputDown == 0x20);
static_assert(coop::th12::kInputLeft == 0x40);
static_assert(coop::th12::kInputRight == 0x80);

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FUNCTION__ << ": check failed at line "         \
                      << __LINE__ << ": " #condition "\n";                 \
            return false;                                                    \
        }                                                                    \
    } while (false)

bool MapsEveryMovementFocusAndShootBit() {
    using namespace coop::th12;
    CHECK(BuildPlayer2InputMask({}) == 0);
    CHECK(BuildPlayer2InputMask({true, false, false, false, false, false, false}) ==
          kInputLeft);
    CHECK(BuildPlayer2InputMask({false, true, false, false, false, false, false}) ==
          kInputRight);
    CHECK(BuildPlayer2InputMask({false, false, true, false, false, false, false}) ==
          kInputUp);
    CHECK(BuildPlayer2InputMask({false, false, false, true, false, false, false}) ==
          kInputDown);
    CHECK(BuildPlayer2InputMask({false, false, false, false, true, false, false}) ==
          kInputFocus);
    CHECK(BuildPlayer2InputMask({false, false, false, false, false, true, false}) ==
          kInputShoot);
    CHECK(BuildPlayer2InputMask({false, false, false, false, false, false, true}) ==
          kInputBomb);
    CHECK(BuildPlayer2InputMask({true, false, true, false, true, true, true}) ==
          (kInputLeft | kInputUp | kInputFocus | kInputShoot | kInputBomb));
    CHECK(BuildPlayer2InputMask({true, false, true, false, true, false, false}) ==
          (kInputLeft | kInputUp | kInputFocus));
    return true;
}

bool MatchesTh12MovementMachineCode() {
    using namespace coop::th12;
    CHECK(BuildPlayer2InputMask({true, false, true, false, true, true, true}) == 0x5B);
    CHECK(BuildPlayer2InputMask({false, true, false, true, false, false, false}) == 0xA0);
    return true;
}

bool KeyBindingsMustBeDistinctVirtualKeys() {
    coop::th12::Player2KeyBindings bindings{65, 68, 87, 83, 32, 74, 75};
    CHECK(bindings.IsValid());
    bindings.focus = 65;
    CHECK(!bindings.IsValid());
    bindings.focus = 0;
    CHECK(!bindings.IsValid());
    bindings.focus = 0xFF;
    CHECK(!bindings.IsValid());
    bindings.focus = 32;
    bindings.shoot = 65;
    CHECK(!bindings.IsValid());
    bindings.shoot = 74;
    bindings.bomb = 74;
    CHECK(!bindings.IsValid());
    return true;
}

bool RestoresIdleOnlyAfterHorizontalRelease() {
    using namespace coop::th12;
    CHECK(ShouldRestoreIdleAnimation(-1152, 0, 0));
    CHECK(ShouldRestoreIdleAnimation(1152, 0, kInputUp));
    CHECK(!ShouldRestoreIdleAnimation(0, 0, 0));
    CHECK(!ShouldRestoreIdleAnimation(1152, 1152, kInputRight));
    CHECK(!ShouldRestoreIdleAnimation(1152, 0, kInputRight));
    return true;
}

}  // namespace

int main() {
    if (!MapsEveryMovementFocusAndShootBit() ||
        !MatchesTh12MovementMachineCode() ||
        !KeyBindingsMustBeDistinctVirtualKeys() ||
        !RestoresIdleOnlyAfterHorizontalRelease()) {
        return 1;
    }
    std::cout << "player 2 input tests passed\n";
    return 0;
}
