#include "collision_ownership.h"

#include <cassert>

int main() {
    using coop::ContactOwner;

    assert(coop::CombinePlayerCollisionResults(0, 0) == 0);
    assert(coop::CombinePlayerCollisionResults(2, 0) == 2);
    assert(coop::CombinePlayerCollisionResults(0, 2) == 2);
    assert(coop::CombinePlayerCollisionResults(2, 1) == 1);
    assert(coop::CombinePlayerCollisionResults(1, 2) == 1);

    const auto p2Dead = coop::ScaleEnemyDamagePerPlayer(100, 50, 1, 0, 5);
    assert(p2Dead.player2 == 10);
    assert(p2Dead.returned == 110);
    const auto p1Dead = coop::ScaleEnemyDamagePerPlayer(100, 50, 0, 1, 5);
    assert(p1Dead.returned == 350);
    const auto bothDead = coop::ScaleEnemyDamagePerPlayer(100, 50, 2, 2, 5);
    assert(bothDead.returned == 150);

    assert(coop::ResolveItemContactOwner(false, false, 0.0f, 0.0f) ==
           ContactOwner::kPlayer1);
    assert(coop::ResolveItemContactOwner(true, false, 4.0f, 1.0f) ==
           ContactOwner::kPlayer1);
    assert(coop::ResolveItemContactOwner(false, true, 1.0f, 4.0f) ==
           ContactOwner::kPlayer2);
    assert(coop::ResolveItemContactOwner(true, true, 4.0f, 1.0f) ==
           ContactOwner::kPlayer2);
    assert(coop::ResolveItemContactOwner(true, true, 1.0f, 1.0f) ==
           ContactOwner::kPlayer1);

    assert(coop::ResolveItemOwnerPriority(0, 0, 9.0f, 1.0f) ==
           ContactOwner::kPlayer1);
    assert(coop::ResolveItemOwnerPriority(3, 1, 9.0f, 1.0f) ==
           ContactOwner::kPlayer1);
    assert(coop::ResolveItemOwnerPriority(1, 2, 1.0f, 9.0f) ==
           ContactOwner::kPlayer2);
    assert(coop::ResolveItemOwnerPriority(2, 2, 9.0f, 1.0f) ==
           ContactOwner::kPlayer2);
    assert(coop::ResolveItemOwnerPriority(2, 2, 1.0f, 1.0f) ==
           ContactOwner::kPlayer1);
    return 0;
}
