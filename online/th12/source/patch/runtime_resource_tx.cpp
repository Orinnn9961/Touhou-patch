#include "runtime_resource_tx.h"

#include "log.h"
#include "runtime_player2.h"
#include "version_map.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <sstream>

namespace coop::th12 {
namespace {

std::wstring g_root;
bool g_configured = false;
bool g_enabled = false;
SharedResourceLedger g_ledger;
bool g_inventoryUiDirty = false;

template <typename T>
T& Global(uintptr_t address) noexcept {
    return *reinterpret_cast<T*>(address);
}

SharedResourceState ReadNative() noexcept {
    SharedResourceState state;
    state.power = Global<int32_t>(GameAddresses::kPower);
    state.lives = Global<int32_t>(GameAddresses::kLives);
    state.lifeFragments = Global<int32_t>(GameAddresses::kLifeFragments);
    state.bombs = Global<int32_t>(GameAddresses::kBombs);
    state.bombFragments = Global<int32_t>(GameAddresses::kBombFragments);
    state.score = Global<int32_t>(GameAddresses::kScore);
    state.pointValue = Global<int32_t>(GameAddresses::kPointValue);
    state.ufo[0] = Global<int32_t>(GameAddresses::kUfo0);
    state.ufo[1] = Global<int32_t>(GameAddresses::kUfo1);
    state.ufo[2] = Global<int32_t>(GameAddresses::kUfo2);
    state.ufo[3] = Global<int32_t>(GameAddresses::kUfoState);
    state.ufo[4] = Global<int32_t>(GameAddresses::kUfoFlags);
    return state;
}

void WriteNative(const SharedResourceState& state) noexcept {
    Global<int32_t>(GameAddresses::kPower) = state.power;
    Global<int32_t>(GameAddresses::kLives) = state.lives;
    Global<int32_t>(GameAddresses::kLifeFragments) = state.lifeFragments;
    Global<int32_t>(GameAddresses::kBombs) = state.bombs;
    Global<int32_t>(GameAddresses::kBombFragments) = state.bombFragments;
    Global<int32_t>(GameAddresses::kScore) = state.score;
    Global<int32_t>(GameAddresses::kPointValue) = state.pointValue;
    Global<int32_t>(GameAddresses::kUfo0) = state.ufo[0];
    Global<int32_t>(GameAddresses::kUfo1) = state.ufo[1];
    Global<int32_t>(GameAddresses::kUfo2) = state.ufo[2];
    Global<int32_t>(GameAddresses::kUfoState) = state.ufo[3];
    Global<int32_t>(GameAddresses::kUfoFlags) = state.ufo[4];
}

void SyncInventoryUi(const SharedResourceState& before,
                     const SharedResourceState& after,
                     bool forceLives = false) noexcept {
    void* ui = Global<void*>(GameAddresses::kLifeUiManager);
    if (ui == nullptr) {
        return;
    }
    using UpdateFunction = void (__stdcall*)(void*, int32_t, int32_t);
    if (forceLives || before.lives != after.lives ||
        before.lifeFragments != after.lifeFragments) {
        reinterpret_cast<UpdateFunction>(GameAddresses::kLifeUiUpdate)(
            ui, after.lives, after.lifeFragments);
    }
    if (before.bombs != after.bombs ||
        before.bombFragments != after.bombFragments) {
        reinterpret_cast<UpdateFunction>(GameAddresses::kBombUiUpdate)(
            ui, after.bombs, after.bombFragments);
    }
}

int32_t Value(const SharedResourceState& state,
              SharedResource resource) noexcept {
    switch (resource) {
    case SharedResource::kPower: return state.power;
    case SharedResource::kLives: return state.lives;
    case SharedResource::kLifeFragments: return state.lifeFragments;
    case SharedResource::kBombs: return state.bombs;
    case SharedResource::kBombFragments: return state.bombFragments;
    case SharedResource::kScore: return state.score;
    case SharedResource::kPointValue: return state.pointValue;
    case SharedResource::kUfo0: return state.ufo[0];
    case SharedResource::kUfo1: return state.ufo[1];
    case SharedResource::kUfo2: return state.ufo[2];
    case SharedResource::kUfoState: return state.ufo[3];
    case SharedResource::kUfoFlags: return state.ufo[4];
    }
    return 0;
}

template <typename Callback>
void ForEachValue(const SharedResourceState& before,
                  const SharedResourceState& after, uint8_t player,
                  Callback&& callback) noexcept {
    constexpr std::array<SharedResource, 12> resources{
        SharedResource::kPower, SharedResource::kLives,
        SharedResource::kLifeFragments, SharedResource::kBombs,
        SharedResource::kBombFragments, SharedResource::kScore,
        SharedResource::kPointValue, SharedResource::kUfo0,
        SharedResource::kUfo1, SharedResource::kUfo2,
        SharedResource::kUfoState, SharedResource::kUfoFlags};
    for (const auto resource : resources) {
        const int64_t delta = static_cast<int64_t>(Value(after, resource)) -
                              Value(before, resource);
        if (delta != 0) {
            callback(resource, static_cast<int32_t>(delta), player);
        }
    }
}

}  // namespace

bool InitializeRuntimeResourceTransactions(const std::wstring& root,
                                           std::wstring& error) noexcept {
    if (g_configured) {
        return true;
    }
    g_root = root;
    const std::wstring configPath = root + L"\\coop\\config.ini";
    g_enabled = GetPrivateProfileIntW(L"phase11", L"enabled", 1,
                                      configPath.c_str()) != 0;
    g_configured = true;
    if (!g_enabled) {
        return true;
    }
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) !=
        GameAddresses::kImageBase) {
        error = L"phase11 requires the TH12 fixed image base";
        g_enabled = false;
        return false;
    }
    coop::WriteLog(g_root, L"patch.log",
                   L"phase11 shared resource transaction layer armed; scheduler-order settlement");
    return true;
}

bool RuntimeResourceTransactionsEnabled() noexcept {
    return g_enabled;
}

bool RuntimeResourceFrameActive() noexcept {
    return g_enabled && g_ledger.Active();
}

void BeginRuntimeResourceFrame() noexcept {
    if (!g_enabled) {
        return;
    }
    if (g_ledger.Active()) {
        const SharedResourceState before = g_ledger.Committed();
        const SharedResourceState after = g_ledger.Commit();
        WriteNative(after);
        SyncInventoryUi(before, after, g_inventoryUiDirty);
        g_inventoryUiDirty = false;
    }
    g_ledger.Begin(ReadNative());
}

void ExposeRuntimeResourceProjection() noexcept {
    if (g_enabled && g_ledger.Active()) {
        WriteNative(g_ledger.Projected());
    }
}

void CaptureRuntimeNativeResourceChanges(uint8_t player) noexcept {
    if (!g_enabled || !g_ledger.Active()) {
        return;
    }
    const SharedResourceState projected = g_ledger.Projected();
    const SharedResourceState observed = ReadNative();
    ForEachValue(projected, observed, player,
                 [](SharedResource resource, int32_t delta, uint8_t owner) {
                     if (resource == SharedResource::kLives && delta > 0) {
                         const int32_t adjusted =
                             ConsumeRuntimeLifeGainForRevival(delta);
                         g_inventoryUiDirty =
                             g_inventoryUiDirty || adjusted != delta;
                         delta = adjusted;
                     }
                     g_ledger.Queue(resource, delta, owner);
                 });
    // Keep native globals on the new projection. Leaving them on the snapshot
    // from before these events makes the next original subsystem observe stale
    // fragments/UFO slots and can undo state-machine resets.
    WriteNative(g_ledger.Projected());
}

void CommitRuntimeResourceFrame() noexcept {
    if (!g_enabled || !g_ledger.Active()) {
        return;
    }
    const SharedResourceState before = g_ledger.Committed();
    const SharedResourceState after = g_ledger.Commit();
    WriteNative(after);
    SyncInventoryUi(before, after, g_inventoryUiDirty);
    g_inventoryUiDirty = false;
}

void AbortRuntimeResourceFrame() noexcept {
    if (!g_enabled || !g_ledger.Active()) {
        return;
    }
    WriteNative(g_ledger.Projected());
    g_ledger.Abort();
    g_inventoryUiDirty = false;
}

void QueueRuntimeResourceDelta(SharedResource resource, int32_t delta,
                               uint8_t player) noexcept {
    if (g_enabled) {
        if (!g_ledger.Active()) {
            BeginRuntimeResourceFrame();
        }
        g_ledger.Queue(resource, delta, player);
        WriteNative(g_ledger.Projected());
    }
}

int32_t RuntimeProjectedResource(SharedResource resource) noexcept {
    if (!g_enabled || !g_ledger.Active()) {
        return Value(ReadNative(), resource);
    }
    return Value(g_ledger.Projected(), resource);
}

}  // namespace coop::th12
