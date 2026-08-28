#include "boss_health.h"

#include <cassert>
#include <climits>
#include <cstdint>

int main() {
    assert(coop::ScaleBossLife50(-1) == -1);
    assert(coop::ScaleBossLife50(0) == 0);
    assert(coop::ScaleBossLife50(1) == 1);
    assert(coop::ScaleBossLife50(2) == 3);
    assert(coop::ScaleBossLife50(3) == 4);
    assert(coop::ScaleBossLife50(100) == 150);
    assert(coop::ScaleBossLife50(1431655765) == INT32_MAX);
    assert(coop::ScaleBossLife50(1431655766) == INT32_MAX);
    assert(coop::ScaleBossLife50(INT32_MAX) == INT32_MAX);

    uint32_t remainder = 0;
    assert(coop::ScaleBossDamageFor150PercentLife(-1, remainder) == -1);
    assert(remainder == 0);
    assert(coop::ScaleBossDamageFor150PercentLife(0, remainder) == 0);
    assert(remainder == 0);
    assert(coop::ScaleBossDamageFor150PercentLife(1, remainder) == 0);
    assert(remainder == 2);
    assert(coop::ScaleBossDamageFor150PercentLife(1, remainder) == 1);
    assert(remainder == 1);
    assert(coop::ScaleBossDamageFor150PercentLife(1, remainder) == 1);
    assert(remainder == 0);
    assert(coop::ScaleBossDamageFor150PercentLife(300, remainder) == 200);
    assert(remainder == 0);
    assert(coop::ScaleBossDamageFor150PercentLife(INT32_MAX, remainder) ==
           1431655764);
    assert(remainder == 2);
#ifdef NDEBUG
    // The assertions above are compiled out in Release; keep the test input
    // considered used so /W4 /WX does not reject this translation unit.
    (void)remainder;
#endif
    return 0;
}
