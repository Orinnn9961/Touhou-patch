#include "runtime_dialogue.h"

#include "log.h"
#include "memory_patch.h"
#include "version_map.h"

#include <windows.h>

namespace coop::th12 {
namespace {

RelativeCallPatch g_enemyMessageUpdatePatch;
RelativeCallPatch g_callbackMessageUpdatePatch;
bool g_initialized = false;

using MessageUpdate = int(__stdcall*)(void* messageState);

int __stdcall MessageUpdateHook(void* messageState) noexcept {
    // MessageState::flags bit 0 is the native "waiting for confirmation"
    // state. 0x200 is TH12's message-advance input, not the gameplay Z bit.
    const bool waitingForConfirmation = messageState != nullptr &&
        (*reinterpret_cast<volatile uint32_t*>(
             reinterpret_cast<uintptr_t>(messageState) + 0x90U) &
         0x01U) != 0;
    const auto original =
        reinterpret_cast<MessageUpdate>(GameAddresses::kMessageUpdate);
    if (!waitingForConfirmation) {
        return original(messageState);
    }

    auto* input = reinterpret_cast<volatile uint32_t*>(
        GameAddresses::kInputMask);
    const uint32_t savedInput = *input;
    *input = savedInput | 0x200U;
    const int result = original(messageState);
    *input = savedInput;
    return result;
}

}  // namespace

bool InitializeRuntimeDialogueSkip(const std::wstring& root,
                                   std::wstring& error) noexcept {
    if (g_initialized) {
        return true;
    }

    const std::wstring configPath = root + L"\\coop\\config.ini";
    if (GetPrivateProfileIntW(L"phase6", L"auto_skip_dialogue", 1,
                              configPath.c_str()) == 0) {
        g_initialized = true;
        coop::WriteLog(root, L"patch.log",
                       L"phase13 dialogue auto-skip disabled by config");
        return true;
    }

    // Scope the message-advance bit to the actual interpreter call. The input
    // mask is restored before any player update can observe it.
    g_enemyMessageUpdatePatch = RelativeCallPatch(
        GameAddresses::kEnemyMessageUpdateCall,
        GameAddresses::kMessageUpdate);
    if (!g_enemyMessageUpdatePatch.Install(
            reinterpret_cast<void*>(&MessageUpdateHook))) {
        error = L"enemy message interpreter call signature does not match th12 1.00b";
        return false;
    }

    g_callbackMessageUpdatePatch = RelativeCallPatch(
        GameAddresses::kCallbackMessageUpdateCall,
        GameAddresses::kMessageUpdate);
    if (!g_callbackMessageUpdatePatch.Install(
            reinterpret_cast<void*>(&MessageUpdateHook))) {
        g_enemyMessageUpdatePatch.Restore();
        error = L"callback message interpreter call signature does not match th12 1.00b";
        return false;
    }

    g_initialized = true;
    coop::WriteLog(
        root, L"patch.log",
        L"phase13 message-interpreter auto-skip armed; enemy and callback paths covered; skip input scoped to native dialogue waits");
    return true;
}

}  // namespace coop::th12
