#pragma once

#include <cstdint>

namespace coop {

enum CoopRule : uint8_t {
    kCoopRuleLockPower = 1U << 0U,
    kCoopRuleLockLives = 1U << 1U,
    kCoopRuleAutoBomb = 1U << 2U,
    kCoopRuleInfiniteRespawn = 1U << 3U,
};

constexpr uint8_t kCoopRuleMask =
    kCoopRuleLockPower | kCoopRuleLockLives | kCoopRuleAutoBomb |
    kCoopRuleInfiniteRespawn;

static_assert((kCoopRuleLockPower & kCoopRuleLockLives) == 0);
static_assert((kCoopRuleLockPower & kCoopRuleAutoBomb) == 0);
static_assert((kCoopRuleLockLives & kCoopRuleAutoBomb) == 0);
static_assert((kCoopRuleLockPower & kCoopRuleInfiniteRespawn) == 0);
static_assert((kCoopRuleLockLives & kCoopRuleInfiniteRespawn) == 0);
static_assert((kCoopRuleAutoBomb & kCoopRuleInfiniteRespawn) == 0);

constexpr bool CoopRuleEnabled(uint8_t rules, CoopRule rule) noexcept {
    return (rules & static_cast<uint8_t>(rule)) != 0;
}

}  // namespace coop
