#pragma once

#include <cstdint>

namespace coop {

int32_t ScaleBossLife50(int32_t baseLife) noexcept;
int32_t ScaleBossDamageFor150PercentLife(int32_t damage,
                                         uint32_t& remainder) noexcept;

}  // namespace coop
