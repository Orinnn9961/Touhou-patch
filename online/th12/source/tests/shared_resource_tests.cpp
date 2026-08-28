#include "shared_resource.h"

#include <cassert>

using namespace coop::th12;

int main() {
    assert(PowerAfterDeathLoss(400, 100) == 300);
    assert(PowerAfterDeathLoss(150, 100) == 100);
    assert(PowerAfterDeathLoss(100, 100) == 100);
    assert(PowerAfterDeathLoss(400, 50) == 350);
    assert(PowerAfterDeathLoss(125, 50) == 100);
    assert(PowerAfterDeathLoss(100, 50) == 100);

    SharedResourceLedger ledger;
    SharedResourceState initial{};
    initial.power = 100;
    initial.bombs = 1;
    initial.score = 500;
    initial.ufo[0] = 2;
    ledger.Begin(initial);
    ledger.Queue(SharedResource::kBombs, -1, 1);
    ledger.Queue(SharedResource::kBombs, -1, 0);
    ledger.Queue(SharedResource::kPower, -20, 1);
    ledger.Queue(SharedResource::kScore, 300, 0);
    const SharedResourceState projected = ledger.Projected();
    assert(projected.bombs == 0);
    assert(projected.power == 80);
    const SharedResourceState committed = ledger.Commit();
    assert(committed.bombs == 0);
    assert(committed.power == 80);
    assert(committed.score == 800);
    assert(!ledger.Active());

    ledger.Begin(initial);
    ledger.Queue(SharedResource::kBombs, 5, 0);
    assert(ledger.Commit().bombs == 6);

    initial.bombs = 8;
    ledger.Begin(initial);
    ledger.Queue(SharedResource::kBombs, -1, 1);
    ledger.Queue(SharedResource::kBombs, 1, 0);
    assert(ledger.Projected().bombs == 8);
    assert(ledger.Commit().bombs == 8);

    initial.lifeFragments = 3;
    initial.bombFragments = 2;
    ledger.Begin(initial);
    ledger.Queue(SharedResource::kLifeFragments, 1, 0);
    ledger.Queue(SharedResource::kBombFragments, 2, 1);
    const SharedResourceState fragmentCommit = ledger.Commit();
    assert(fragmentCommit.lifeFragments == 4);
    assert(fragmentCommit.bombFragments == 4);

    initial.ufo[0] = 0;
    ledger.Begin(initial);
    ledger.Queue(SharedResource::kUfo0, 1, 1);
    ledger.Queue(SharedResource::kUfo0, 1, 1);
    ledger.Queue(SharedResource::kUfo0, 1, 1);
    ledger.Queue(SharedResource::kUfo0, -3, 0);
    ledger.Queue(SharedResource::kScore, 1000, 1);
    const SharedResourceState ufoCommit = ledger.Commit();
    assert(ufoCommit.ufo[0] == 0);
    assert(ufoCommit.score == 1500);
    return 0;
}
