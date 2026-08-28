#pragma once

#include <cstdint>

namespace coop::th12 {

struct GameRngStreamState {
    uint16_t seed{};
    uint32_t calls{};
};

struct GameRngState {
    GameRngStreamState primary{};
    GameRngStreamState secondary{};
};

inline bool operator==(const GameRngState& left,
                       const GameRngState& right) noexcept {
    return left.primary.seed == right.primary.seed &&
           left.primary.calls == right.primary.calls &&
           left.secondary.seed == right.secondary.seed &&
           left.secondary.calls == right.secondary.calls;
}

inline bool operator!=(const GameRngState& left,
                       const GameRngState& right) noexcept {
    return !(left == right);
}

}  // namespace coop::th12
