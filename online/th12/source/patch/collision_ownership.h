#pragma once

#include <cstdint>

namespace coop {

enum class ContactOwner : uint8_t {
    kPlayer1 = 0,
    kPlayer2 = 1,
};

int CombinePlayerCollisionResults(int player1Result,
                                  int player2Result) noexcept;

// Returns the result visible to the original caller.  The two per-player
// results remain available to the caller through the graze dispatch mask.
int NativeCollisionResult(int player1Result, int player2Result) noexcept;

struct EnemyDamageResult {
    int32_t player1{};
    int32_t player2{};
    int32_t returned{};
};

EnemyDamageResult ScaleEnemyDamagePerPlayer(int32_t player1Damage,
                                            int32_t player2Damage,
                                            int32_t player1State,
                                            int32_t player2State,
                                            int32_t deadPlayerDivisor) noexcept;
ContactOwner ResolveItemContactOwner(bool player1Contact, bool player2Contact,
                                     float player1DistanceSquared,
                                     float player2DistanceSquared) noexcept;
ContactOwner ResolveItemOwnerPriority(uint8_t player1Priority,
                                      uint8_t player2Priority,
                                      float player1DistanceSquared,
                                      float player2DistanceSquared) noexcept;

}  // namespace coop
