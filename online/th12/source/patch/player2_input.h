#pragma once

#include <cstdint>

namespace coop::th12 {

constexpr uint32_t kInputShoot = 0x01;
constexpr uint32_t kInputBomb = 0x02;
constexpr uint32_t kInputFocus = 0x08;
constexpr uint32_t kInputUp = 0x10;
constexpr uint32_t kInputDown = 0x20;
constexpr uint32_t kInputLeft = 0x40;
constexpr uint32_t kInputRight = 0x80;
constexpr uint32_t kInputMovementMask =
    kInputLeft | kInputRight | kInputUp | kInputDown;

struct Player2Buttons {
    bool left{};
    bool right{};
    bool up{};
    bool down{};
    bool focus{};
    bool shoot{};
    bool bomb{};
};

struct Player2KeyBindings {
    uint32_t left{};
    uint32_t right{};
    uint32_t up{};
    uint32_t down{};
    uint32_t focus{};
    uint32_t shoot{};
    uint32_t bomb{};

    bool IsValid() const noexcept;
};

uint32_t BuildPlayer2InputMask(const Player2Buttons& buttons) noexcept;
bool ShouldRestoreIdleAnimation(int32_t previousHorizontalVelocity,
                                int32_t currentHorizontalVelocity,
                                uint32_t inputMask) noexcept;

}  // namespace coop::th12
