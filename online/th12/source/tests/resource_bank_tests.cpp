#include "resource_bank.h"

#include <cstdint>
#include <iostream>

namespace {

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::cerr << __FUNCTION__ << ": check failed at line "          \
                      << __LINE__ << ": " #condition "\n";                  \
            return false;                                                     \
        }                                                                     \
    } while (false)

bool AllAirframePairsHaveAnExplicitPlan() {
    for (uint32_t player1 = 0; player1 < 6; ++player1) {
        for (uint32_t player2 = 0; player2 < 6; ++player2) {
            coop::th12::ResourceLoadPlan plan;
            CHECK(coop::th12::BuildResourceLoadPlan(player1, player2, plan));
            CHECK(plan.player1Airframe == player1);
            CHECK(plan.player2Airframe == player2);
            CHECK(plan.shareAnmArchive == (player1 / 2 == player2 / 2));
        }
    }
    coop::th12::ResourceLoadPlan invalid;
    CHECK(!coop::th12::BuildResourceLoadPlan(6, 0, invalid));
    CHECK(!coop::th12::BuildResourceLoadPlan(0, 6, invalid));
    return true;
}

bool BanksRejectSharedMutableSht() {
    int anm = 1;
    int sharedSht = 2;
    int isolatedSht = 3;
    coop::th12::DualResourceBanks banks;
    CHECK(!banks.SetPlayer1({6, &anm, &sharedSht, false, false}));
    CHECK(banks.SetPlayer1({0, &anm, &sharedSht, false,
                           false}));
    CHECK(!banks.SetPlayer2({1, &anm, &sharedSht, false,
                             true}));
    CHECK(!banks.IsReady());
    CHECK(banks.Player2() == nullptr);
    CHECK(banks.SetPlayer2({1, &anm, &isolatedSht, false,
                           true}));
    CHECK(banks.IsReady());
    CHECK(banks.HasNoCrossPlayerCollision());
    return true;
}

bool DifferentCharactersRequireDifferentArchives() {
    int player1Anm = 1;
    int player1Sht = 2;
    int player2Sht = 3;
    int player2Anm = 4;
    coop::th12::DualResourceBanks banks;
    CHECK(banks.SetPlayer1({0, &player1Anm, &player1Sht, false,
                           false}));
    CHECK(!banks.SetPlayer2({2, &player1Anm, &player2Sht, true,
                             true}));
    CHECK(!banks.IsReady());
    CHECK(banks.Player2() == nullptr);
    CHECK(banks.SetPlayer2({2, &player2Anm, &player2Sht, true,
                           true}));
    CHECK(banks.IsReady());
    CHECK(banks.HasNoCrossPlayerCollision());
    CHECK(!banks.SetPlayer1({4, &player2Anm, &player1Sht, false,
                             false}));
    CHECK(banks.Player1()->airframeIndex == 0);
    banks.ClearPlayer2();
    CHECK(!banks.IsReady());
    CHECK(banks.Player2() == nullptr);
    return true;
}

}  // namespace

int main() {
    if (!AllAirframePairsHaveAnExplicitPlan() || !BanksRejectSharedMutableSht() ||
        !DifferentCharactersRequireDifferentArchives()) {
        return 1;
    }
    std::cout << "resource bank tests passed\n";
    return 0;
}
