#include "shared_resource.h"

#include <algorithm>

namespace coop::th12 {
namespace {

int32_t& Value(SharedResourceState& state, SharedResource resource) noexcept {
    switch (resource) {
    case SharedResource::kPower:
        return state.power;
    case SharedResource::kLives:
        return state.lives;
    case SharedResource::kLifeFragments:
        return state.lifeFragments;
    case SharedResource::kBombs:
        return state.bombs;
    case SharedResource::kBombFragments:
        return state.bombFragments;
    case SharedResource::kScore:
        return state.score;
    case SharedResource::kPointValue:
        return state.pointValue;
    case SharedResource::kUfo0:
        return state.ufo[0];
    case SharedResource::kUfo1:
        return state.ufo[1];
    case SharedResource::kUfo2:
        return state.ufo[2];
    case SharedResource::kUfoState:
        return state.ufo[3];
    case SharedResource::kUfoFlags:
        return state.ufo[4];
    }
    return state.power;
}

int32_t ClampValue(SharedResource resource, int64_t value) noexcept {
    int64_t minimum = INT32_MIN;
    int64_t maximum = INT32_MAX;
    switch (resource) {
    case SharedResource::kPower:
        minimum = 0;
        maximum = 400;
        break;
    case SharedResource::kLives:
        minimum = 0;
        maximum = 9;
        break;
    case SharedResource::kLifeFragments:
    case SharedResource::kBombFragments:
        minimum = 0;
        // TH12 converts fragments at five. Four is therefore the largest
        // stable value that can be displayed between collection events.
        maximum = 4;
        break;
    case SharedResource::kBombs:
        minimum = 0;
        maximum = 8;
        break;
    case SharedResource::kScore:
        minimum = 0;
        maximum = 0x3B9AC9FFLL;
        break;
    case SharedResource::kPointValue:
        minimum = -1024;
        maximum = 1024;
        break;
    case SharedResource::kUfo0:
    case SharedResource::kUfo1:
    case SharedResource::kUfo2:
    case SharedResource::kUfoState:
    case SharedResource::kUfoFlags:
        minimum = 0;
        maximum = INT32_MAX;
        break;
    }
    return static_cast<int32_t>((std::max)(minimum, (std::min)(maximum, value)));
}

void Apply(SharedResourceState& state, const SharedResourceEvent& event) noexcept {
    int32_t& value = Value(state, event.resource);
    value = ClampValue(event.resource,
                       static_cast<int64_t>(value) + event.delta);
}

}  // namespace

int32_t PowerAfterDeathLoss(int32_t currentPower, int32_t loss) noexcept {
    constexpr int64_t minimumPower = 100;
    const int64_t reduced = static_cast<int64_t>(currentPower) -
                            static_cast<int64_t>(loss);
    return static_cast<int32_t>((std::min)(
        int64_t{400}, (std::max)(minimumPower, reduced)));
}

void SharedResourceLedger::Begin(const SharedResourceState& committed) noexcept {
    committed_ = committed;
    events_.clear();
    nextSequence_ = 0;
    active_ = true;
}

void SharedResourceLedger::Queue(SharedResource resource, int32_t delta,
                                 uint8_t player) noexcept {
    if (!active_ || delta == 0) {
        return;
    }
    events_.push_back({resource, delta, player, nextSequence_++});
}

SharedResourceState SharedResourceLedger::Projected() const noexcept {
    SharedResourceState projected = committed_;
    for (const auto& event : events_) {
        Apply(projected, event);
    }
    return projected;
}

SharedResourceState SharedResourceLedger::Commit() noexcept {
    if (!active_) {
        return committed_;
    }
    // Scheduler order is already deterministic. Preserve it so system resets
    // cannot move ahead of the player events that caused them.
    for (const auto& event : events_) {
        Apply(committed_, event);
    }
    events_.clear();
    active_ = false;
    return committed_;
}

void SharedResourceLedger::Abort() noexcept {
    events_.clear();
    active_ = false;
}

}  // namespace coop::th12
