#include "boss_health.h"

#include <climits>

namespace coop {

int32_t ScaleBossLife50(int32_t baseLife) noexcept {
    if (baseLife <= 0) {
        return baseLife;
    }
    if (baseLife > INT32_MAX / 3 * 2 + 1) {
        return INT32_MAX;
    }
    return baseLife + baseLife / 2;
}

int32_t ScaleBossDamageFor150PercentLife(int32_t damage,
                                         uint32_t& remainder) noexcept {
    if (damage <= 0) {
        return damage;
    }
    const int64_t numerator = static_cast<int64_t>(damage) * 2 + remainder;
    remainder = static_cast<uint32_t>(numerator % 3);
    return static_cast<int32_t>(numerator / 3);
}

}  // namespace coop
