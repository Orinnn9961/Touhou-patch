#pragma once

#include <cstdint>
#include <string>

namespace coop::th12 {

struct Phase9Counters {
    uint64_t hits[2]{};
    uint64_t grazes[2]{};
    uint64_t itemContacts[2]{};
    uint64_t damageScans[2]{};
};

bool InitializeRuntimeCollisions(const std::wstring& root,
                                 std::wstring& error) noexcept;
const Phase9Counters& RuntimePhase9Counters() noexcept;

}  // namespace coop::th12
