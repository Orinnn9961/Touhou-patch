#include "runtime_player_context.h"

#include "version_map.h"

namespace coop::th12 {
namespace {

PlayerContextManager g_playerContexts;
bool g_ready = false;

}  // namespace

bool InitializeRuntimePlayerContexts(std::wstring& error) noexcept {
    if (g_ready) {
        return true;
    }

    const GameGlobalBindings bindings{
        reinterpret_cast<volatile int32_t*>(GameAddresses::kCharacter),
        reinterpret_cast<volatile int32_t*>(GameAddresses::kShotType),
        reinterpret_cast<volatile uint32_t*>(GameAddresses::kInputMask),
        reinterpret_cast<void* volatile*>(GameAddresses::kPlayerSingleton),
    };
    if (!g_playerContexts.InitializePlayer1(bindings)) {
        error = L"unable to bind phase 3 player globals";
        return false;
    }
    g_ready = true;
    return true;
}

PlayerContextManager* RuntimePlayerContexts() noexcept {
    return g_ready ? &g_playerContexts : nullptr;
}

}  // namespace coop::th12
