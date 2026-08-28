#pragma once

#include <cstdint>

namespace coop::th12 {

struct PlayerResourceBank {
    uint32_t airframeIndex{UINT32_MAX};
    void* anmArchive{};
    void* shtData{};
    bool ownsAnmArchive{};
    bool ownsShtData{};

    bool IsReady() const noexcept;
};

struct ResourceLoadPlan {
    uint32_t player1Airframe{UINT32_MAX};
    uint32_t player2Airframe{UINT32_MAX};
    bool shareAnmArchive{};
};

bool BuildResourceLoadPlan(uint32_t player1Airframe, uint32_t player2Airframe,
                          ResourceLoadPlan& plan) noexcept;

class DualResourceBanks {
public:
    bool SetPlayer1(const PlayerResourceBank& bank) noexcept;
    bool SetPlayer2(const PlayerResourceBank& bank) noexcept;
    void ClearPlayer2() noexcept;
    void Clear() noexcept;

    const PlayerResourceBank* Player1() const noexcept;
    const PlayerResourceBank* Player2() const noexcept;
    bool IsReady() const noexcept;
    bool HasNoCrossPlayerCollision() const noexcept;

private:
    PlayerResourceBank player1_{};
    PlayerResourceBank player2_{};
    bool player1Ready_{};
    bool player2Ready_{};
};

}  // namespace coop::th12
