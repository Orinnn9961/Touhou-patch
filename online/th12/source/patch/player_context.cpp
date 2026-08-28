#include "player_context.h"

namespace coop::th12 {

bool PlayerContext::HasValidAirframe() const noexcept {
    return character >= 0 && character <= 2 && shotType >= 0 && shotType <= 1;
}

uint32_t PlayerContext::AirframeIndex() const noexcept {
    if (!HasValidAirframe()) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(shotType + character * 2);
}

bool GameGlobalBindings::IsComplete() const noexcept {
    return character != nullptr && shotType != nullptr &&
           inputMask != nullptr && playerInf != nullptr;
}

bool PlayerContextManager::InitializePlayer1(GameGlobalBindings bindings) noexcept {
    if (!bindings.IsComplete()) {
        return false;
    }
    bindings_ = bindings;
    active_ = PlayerSlot::kPlayer1;
    contexts_[Index(active_)] = ReadGlobals();
    valid_.fill(false);
    valid_[Index(active_)] = true;
    initialized_ = true;
    return true;
}

bool PlayerContextManager::IsInitialized() const noexcept {
    return initialized_;
}

PlayerSlot PlayerContextManager::ActiveSlot() const noexcept {
    return active_;
}

bool PlayerContextManager::HasContext(PlayerSlot slot) const noexcept {
    const size_t index = Index(slot);
    return initialized_ && index < kPlayerCount && valid_[index];
}

const PlayerContext* PlayerContextManager::GetContext(PlayerSlot slot) const noexcept {
    if (!HasContext(slot)) {
        return nullptr;
    }
    return &contexts_[Index(slot)];
}

bool PlayerContextManager::SnapshotActive() noexcept {
    if (!initialized_) {
        return false;
    }
    contexts_[Index(active_)] = ReadGlobals();
    valid_[Index(active_)] = true;
    return true;
}

bool PlayerContextManager::ConfigureInactive(PlayerSlot slot,
                                             const PlayerContext& context) noexcept {
    const size_t index = Index(slot);
    if (!initialized_ || index >= kPlayerCount || slot == active_) {
        return false;
    }
    contexts_[index] = context;
    valid_[index] = true;
    return true;
}

bool PlayerContextManager::ClearInactive(PlayerSlot slot) noexcept {
    const size_t index = Index(slot);
    if (!initialized_ || index >= kPlayerCount || slot == active_) {
        return false;
    }
    contexts_[index] = {};
    valid_[index] = false;
    return true;
}

bool PlayerContextManager::Activate(PlayerSlot slot) noexcept {
    const size_t targetIndex = Index(slot);
    if (!initialized_ || targetIndex >= kPlayerCount || !valid_[targetIndex]) {
        return false;
    }
    if (slot == active_) {
        return true;
    }

    contexts_[Index(active_)] = ReadGlobals();
    valid_[Index(active_)] = true;
    WriteGlobals(contexts_[targetIndex]);
    active_ = slot;
    return true;
}

size_t PlayerContextManager::Index(PlayerSlot slot) noexcept {
    return static_cast<size_t>(slot);
}

PlayerContext PlayerContextManager::ReadGlobals() const noexcept {
    return PlayerContext{
        *bindings_.character,
        *bindings_.shotType,
        *bindings_.inputMask,
        *bindings_.playerInf,
    };
}

void PlayerContextManager::WriteGlobals(const PlayerContext& context) noexcept {
    *bindings_.character = context.character;
    *bindings_.shotType = context.shotType;
    *bindings_.inputMask = context.inputMask;
    *bindings_.playerInf = context.playerInf;
}

ScopedPlayerContext::ScopedPlayerContext(PlayerContextManager& manager,
                                         PlayerSlot target) noexcept
    : manager_(manager), previous_(manager.ActiveSlot()) {
    if (!manager_.IsInitialized()) {
        return;
    }
    if (target == previous_) {
        active_ = true;
        return;
    }
    switched_ = manager_.Activate(target);
    active_ = switched_;
}

ScopedPlayerContext::~ScopedPlayerContext() {
    if (active_ && switched_) {
        manager_.Activate(previous_);
    }
}

bool ScopedPlayerContext::IsActive() const noexcept {
    return active_;
}

}  // namespace coop::th12
