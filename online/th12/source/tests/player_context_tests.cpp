#include "player_context.h"

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

struct Fixture {
    int32_t character = 0;
    int32_t shotType = 1;
    uint32_t inputMask = 0x15;
    int playerObject = 1;
    void* playerInf = &playerObject;

    coop::th12::GameGlobalBindings Bindings() {
        return {&character, &shotType, &inputMask, &playerInf};
    }
};

bool InitializationIsReadOnly() {
    Fixture fixture;
    coop::th12::PlayerContextManager manager;
    CHECK(manager.InitializePlayer1(fixture.Bindings()));
    CHECK(fixture.character == 0);
    CHECK(fixture.shotType == 1);
    CHECK(fixture.inputMask == 0x15);
    CHECK(fixture.playerInf == &fixture.playerObject);
    CHECK(manager.ActiveSlot() == coop::th12::PlayerSlot::kPlayer1);
    CHECK(manager.GetContext(coop::th12::PlayerSlot::kPlayer1) != nullptr);
    CHECK(manager.GetContext(coop::th12::PlayerSlot::kPlayer1)->AirframeIndex() == 1);
    return true;
}

bool SwitchRoundTripPreservesEveryGlobal() {
    Fixture fixture;
    int secondPlayer = 2;
    int replacementPlayer1 = 3;
    coop::th12::PlayerContextManager manager;
    CHECK(manager.InitializePlayer1(fixture.Bindings()));

    const coop::th12::PlayerContext player2{2, 0, 0x2A, &secondPlayer};
    CHECK(manager.ConfigureInactive(coop::th12::PlayerSlot::kPlayer2, player2));

    // P1 can change after startup; switching must capture the current values.
    fixture.character = 1;
    fixture.shotType = 0;
    fixture.inputMask = 0x91;
    fixture.playerInf = &replacementPlayer1;
    CHECK(manager.Activate(coop::th12::PlayerSlot::kPlayer2));
    CHECK(fixture.character == 2);
    CHECK(fixture.shotType == 0);
    CHECK(fixture.inputMask == 0x2A);
    CHECK(fixture.playerInf == &secondPlayer);

    CHECK(manager.Activate(coop::th12::PlayerSlot::kPlayer1));
    CHECK(fixture.character == 1);
    CHECK(fixture.shotType == 0);
    CHECK(fixture.inputMask == 0x91);
    CHECK(fixture.playerInf == &replacementPlayer1);
    return true;
}

bool ScopedSwitchRestoresPlayer1() {
    Fixture fixture;
    int secondPlayer = 2;
    coop::th12::PlayerContextManager manager;
    CHECK(manager.InitializePlayer1(fixture.Bindings()));
    CHECK(manager.ConfigureInactive(coop::th12::PlayerSlot::kPlayer2,
                                    {2, 1, 0x40, &secondPlayer}));
    {
        coop::th12::ScopedPlayerContext scope(
            manager, coop::th12::PlayerSlot::kPlayer2);
        CHECK(scope.IsActive());
        CHECK(fixture.character == 2);
        fixture.inputMask = 0x44;
    }
    CHECK(manager.ActiveSlot() == coop::th12::PlayerSlot::kPlayer1);
    CHECK(fixture.character == 0);
    CHECK(fixture.shotType == 1);
    CHECK(fixture.inputMask == 0x15);
    CHECK(fixture.playerInf == &fixture.playerObject);
    CHECK(manager.GetContext(coop::th12::PlayerSlot::kPlayer2)->inputMask == 0x44);
    return true;
}

bool InvalidSwitchDoesNotWrite() {
    Fixture fixture;
    coop::th12::PlayerContextManager manager;
    CHECK(manager.InitializePlayer1(fixture.Bindings()));
    CHECK(!manager.Activate(coop::th12::PlayerSlot::kPlayer2));
    CHECK(fixture.character == 0);
    CHECK(fixture.shotType == 1);
    CHECK(fixture.inputMask == 0x15);
    CHECK(fixture.playerInf == &fixture.playerObject);
    CHECK(!manager.ConfigureInactive(coop::th12::PlayerSlot::kPlayer1,
                                     {2, 0, 0, nullptr}));
    return true;
}

bool InactiveContextCanBeCleared() {
    Fixture fixture;
    int secondPlayer = 2;
    coop::th12::PlayerContextManager manager;
    CHECK(manager.InitializePlayer1(fixture.Bindings()));
    CHECK(manager.ConfigureInactive(coop::th12::PlayerSlot::kPlayer2,
                                    {2, 1, 0, &secondPlayer}));
    CHECK(manager.HasContext(coop::th12::PlayerSlot::kPlayer2));
    CHECK(manager.ClearInactive(coop::th12::PlayerSlot::kPlayer2));
    CHECK(!manager.HasContext(coop::th12::PlayerSlot::kPlayer2));
    CHECK(!manager.ClearInactive(coop::th12::PlayerSlot::kPlayer1));
    return true;
}

bool SinglePlayerPassThrough() {
    Fixture fixture;
    int laterPlayer = 4;
    coop::th12::PlayerContextManager manager;
    CHECK(manager.InitializePlayer1(fixture.Bindings()));

    // With no context activation the game remains the sole writer.
    fixture.character = 2;
    fixture.shotType = 0;
    fixture.inputMask = 0xDEADBEEF;
    fixture.playerInf = &laterPlayer;
    CHECK(manager.SnapshotActive());
    CHECK(fixture.character == 2);
    CHECK(fixture.shotType == 0);
    CHECK(fixture.inputMask == 0xDEADBEEF);
    CHECK(fixture.playerInf == &laterPlayer);
    CHECK(manager.GetContext(coop::th12::PlayerSlot::kPlayer1)->playerInf == &laterPlayer);
    return true;
}

}  // namespace

int main() {
    if (!InitializationIsReadOnly() ||
        !SwitchRoundTripPreservesEveryGlobal() ||
        !ScopedSwitchRestoresPlayer1() ||
        !InvalidSwitchDoesNotWrite() ||
        !InactiveContextCanBeCleared() ||
        !SinglePlayerPassThrough()) {
        return 1;
    }
    std::cout << "player context tests passed\n";
    return 0;
}
