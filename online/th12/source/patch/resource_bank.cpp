#include "resource_bank.h"

namespace coop::th12 {

namespace {

constexpr uint32_t kAirframeCount = 6;

bool BanksAreCompatible(const PlayerResourceBank& player1,
                        const PlayerResourceBank& player2) noexcept {
    if (player1.shtData == player2.shtData) {
        return false;
    }
    const bool sameCharacter = player1.airframeIndex / 2 == player2.airframeIndex / 2;
    return sameCharacter || player1.anmArchive != player2.anmArchive;
}

}  // namespace

bool PlayerResourceBank::IsReady() const noexcept {
    return airframeIndex < kAirframeCount && anmArchive != nullptr && shtData != nullptr;
}

bool BuildResourceLoadPlan(uint32_t player1Airframe, uint32_t player2Airframe,
                          ResourceLoadPlan& plan) noexcept {
    if (player1Airframe >= kAirframeCount || player2Airframe >= kAirframeCount) {
        return false;
    }
    plan.player1Airframe = player1Airframe;
    plan.player2Airframe = player2Airframe;
    plan.shareAnmArchive = player1Airframe / 2 == player2Airframe / 2;
    return true;
}

bool DualResourceBanks::SetPlayer1(const PlayerResourceBank& bank) noexcept {
    if (!bank.IsReady() || (player2Ready_ && !BanksAreCompatible(bank, player2_))) {
        return false;
    }
    player1_ = bank;
    player1Ready_ = true;
    return true;
}

bool DualResourceBanks::SetPlayer2(const PlayerResourceBank& bank) noexcept {
    if (!bank.IsReady() || !player1Ready_ || !BanksAreCompatible(player1_, bank)) {
        return false;
    }
    player2_ = bank;
    player2Ready_ = true;
    return true;
}

void DualResourceBanks::ClearPlayer2() noexcept {
    player2_ = {};
    player2Ready_ = false;
}

void DualResourceBanks::Clear() noexcept {
    player1_ = {};
    player2_ = {};
    player1Ready_ = false;
    player2Ready_ = false;
}

const PlayerResourceBank* DualResourceBanks::Player1() const noexcept {
    return player1Ready_ ? &player1_ : nullptr;
}

const PlayerResourceBank* DualResourceBanks::Player2() const noexcept {
    return player2Ready_ ? &player2_ : nullptr;
}

bool DualResourceBanks::IsReady() const noexcept {
    return player1Ready_ && player2Ready_;
}

bool DualResourceBanks::HasNoCrossPlayerCollision() const noexcept {
    if (!IsReady()) {
        return false;
    }
    return BanksAreCompatible(player1_, player2_);
}

}  // namespace coop::th12
