#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace coop::th12 {

enum class PlayerSlot : uint8_t {
    kPlayer1 = 0,
    kPlayer2 = 1,
};

struct PlayerContext {
    int32_t character{};
    int32_t shotType{};
    uint32_t inputMask{};
    void* playerInf{};

    bool HasValidAirframe() const noexcept;
    uint32_t AirframeIndex() const noexcept;
};

struct GameGlobalBindings {
    volatile int32_t* character{};
    volatile int32_t* shotType{};
    volatile uint32_t* inputMask{};
    void* volatile* playerInf{};

    bool IsComplete() const noexcept;
};

class PlayerContextManager {
public:
    // Captures P1 without writing any game global. This is the single-player
    // compatibility boundary used during patch startup.
    bool InitializePlayer1(GameGlobalBindings bindings) noexcept;

    bool IsInitialized() const noexcept;
    PlayerSlot ActiveSlot() const noexcept;
    bool HasContext(PlayerSlot slot) const noexcept;
    const PlayerContext* GetContext(PlayerSlot slot) const noexcept;

    // The active context is always sourced from the live game globals.
    bool SnapshotActive() noexcept;
    bool ConfigureInactive(PlayerSlot slot, const PlayerContext& context) noexcept;
    bool ClearInactive(PlayerSlot slot) noexcept;
    bool Activate(PlayerSlot slot) noexcept;

private:
    static constexpr size_t kPlayerCount = 2;

    static size_t Index(PlayerSlot slot) noexcept;
    PlayerContext ReadGlobals() const noexcept;
    void WriteGlobals(const PlayerContext& context) noexcept;

    GameGlobalBindings bindings_{};
    std::array<PlayerContext, kPlayerCount> contexts_{};
    std::array<bool, kPlayerCount> valid_{};
    PlayerSlot active_{PlayerSlot::kPlayer1};
    bool initialized_{};
};

class ScopedPlayerContext {
public:
    ScopedPlayerContext(PlayerContextManager& manager, PlayerSlot target) noexcept;
    ~ScopedPlayerContext();

    ScopedPlayerContext(const ScopedPlayerContext&) = delete;
    ScopedPlayerContext& operator=(const ScopedPlayerContext&) = delete;

    bool IsActive() const noexcept;

private:
    PlayerContextManager& manager_;
    PlayerSlot previous_;
    bool active_{};
    bool switched_{};
};

}  // namespace coop::th12
