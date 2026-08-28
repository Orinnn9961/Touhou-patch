#include "player2_input.h"

#include <array>

namespace coop::th12 {

bool Player2KeyBindings::IsValid() const noexcept {
    const std::array<uint32_t, 7> keys{
        left, right, up, down, focus, shoot, bomb};
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == 0 || keys[i] > 0xFE) {
            return false;
        }
        for (size_t j = i + 1; j < keys.size(); ++j) {
            if (keys[i] == keys[j]) {
                return false;
            }
        }
    }
    return true;
}

uint32_t BuildPlayer2InputMask(const Player2Buttons& buttons) noexcept {
    uint32_t mask = 0;
    if (buttons.left) {
        mask |= kInputLeft;
    }
    if (buttons.right) {
        mask |= kInputRight;
    }
    if (buttons.up) {
        mask |= kInputUp;
    }
    if (buttons.down) {
        mask |= kInputDown;
    }
    if (buttons.focus) {
        mask |= kInputFocus;
    }
    if (buttons.shoot) {
        mask |= kInputShoot;
    }
    if (buttons.bomb) {
        mask |= kInputBomb;
    }
    return mask;
}

bool ShouldRestoreIdleAnimation(int32_t previousHorizontalVelocity,
                                int32_t currentHorizontalVelocity,
                                uint32_t inputMask) noexcept {
    return previousHorizontalVelocity != 0 && currentHorizontalVelocity == 0 &&
           (inputMask & (kInputLeft | kInputRight)) == 0;
}

}  // namespace coop::th12
