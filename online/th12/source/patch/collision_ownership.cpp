#include "collision_ownership.h"

namespace coop {

int CombinePlayerCollisionResults(int player1Result,
                                  int player2Result) noexcept {
    if (player1Result == 1 || player2Result == 1) {
        return 1;
    }
    return (player1Result == 2 || player2Result == 2) ? 2 : 0;
}

int NativeCollisionResult(int player1Result, int player2Result) noexcept {
    return CombinePlayerCollisionResults(player1Result, player2Result);
}

EnemyDamageResult ScaleEnemyDamagePerPlayer(int32_t player1Damage,
                                            int32_t player2Damage,
                                            int32_t player1State,
                                            int32_t player2State,
                                            int32_t deadPlayerDivisor) noexcept {
    EnemyDamageResult result{player1Damage, player2Damage, 0};
    if (deadPlayerDivisor <= 0) {
        result.returned = player1Damage + player2Damage;
        return result;
    }
    // The original ordinary-enemy caller divides the complete returned value
    // when P1 is in state 0/2. Return a pre-scaled value so each contribution
    // receives the division only when its own player is dead.
    if (player1State != 0 && player1State != 2 &&
        (player2State == 0 || player2State == 2)) {
        result.player2 /= deadPlayerDivisor;
    }
    if (player1State == 0 || player1State == 2) {
        if (player2State != 0 && player2State != 2) {
            result.player2 *= deadPlayerDivisor;
        }
    }
    result.returned = result.player1 + result.player2;
    return result;
}

ContactOwner ResolveItemContactOwner(bool player1Contact, bool player2Contact,
                                     float player1DistanceSquared,
                                     float player2DistanceSquared) noexcept {
    return ResolveItemOwnerPriority(player1Contact ? 1U : 0U,
                                    player2Contact ? 1U : 0U,
                                    player1DistanceSquared,
                                    player2DistanceSquared);
}

ContactOwner ResolveItemOwnerPriority(uint8_t player1Priority,
                                      uint8_t player2Priority,
                                      float player1DistanceSquared,
                                      float player2DistanceSquared) noexcept {
    if (player2Priority > player1Priority ||
        (player2Priority != 0 && player2Priority == player1Priority &&
         player2DistanceSquared < player1DistanceSquared)) {
        return ContactOwner::kPlayer2;
    }
    return ContactOwner::kPlayer1;
}

}  // namespace coop
