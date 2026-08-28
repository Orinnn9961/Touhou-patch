#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace coop::th12 {

enum class SharedResource : uint8_t {
    kPower,
    kLives,
    kLifeFragments,
    kBombs,
    kBombFragments,
    kScore,
    kPointValue,
    kUfo0,
    kUfo1,
    kUfo2,
    kUfoState,
    kUfoFlags,
};

struct SharedResourceState {
    int32_t power{};
    int32_t lives{};
    int32_t lifeFragments{};
    int32_t bombs{};
    int32_t bombFragments{};
    int32_t score{};
    int32_t pointValue{};
    std::array<int32_t, 5> ufo{};
};

struct SharedResourceEvent {
    SharedResource resource{};
    int32_t delta{};
    uint8_t player{};
    uint32_t sequence{};
};

int32_t PowerAfterDeathLoss(int32_t currentPower, int32_t loss) noexcept;

class SharedResourceLedger {
public:
    void Begin(const SharedResourceState& committed) noexcept;
    void Queue(SharedResource resource, int32_t delta,
               uint8_t player) noexcept;
    SharedResourceState Projected() const noexcept;
    SharedResourceState Commit() noexcept;
    void Abort() noexcept;
    bool Active() const noexcept { return active_; }
    const std::vector<SharedResourceEvent>& Events() const noexcept {
        return events_;
    }
    const SharedResourceState& Committed() const noexcept { return committed_; }

private:
    SharedResourceState committed_{};
    std::vector<SharedResourceEvent> events_;
    uint32_t nextSequence_{};
    bool active_{};
};

}  // namespace coop::th12
