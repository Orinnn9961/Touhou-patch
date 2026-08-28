#include "log.h"
#include "path_util.h"
#include "runtime_bomb.h"
#include "runtime_boss.h"
#include "runtime_determinism.h"
#include "runtime_dialogue.h"
#include "runtime_collision.h"
#include "runtime_frame_input.h"
#include "runtime_player_context.h"
#include "runtime_player2.h"
#include "runtime_resources.h"
#include "runtime_resource_tx.h"
#include "version_map.h"

#include <windows.h>
#include <sstream>

namespace {
HMODULE g_module = nullptr;
bool g_initialized = false;
}

extern "C" void WINAPI Th12CoopInitialize() {
    if (g_initialized) {
        return;
    }
    g_initialized = true;
    const std::wstring root = coop::ModuleDirectory(g_module);
    coop::EnableAsyncLogging();
    std::wstringstream message;
    message << L"bootstrap patch loaded; pid=" << GetCurrentProcessId()
            << L"; image_base=0x00400000; phase13 LAN lockstep runtime"
            << L"; build_id=20260820-release-v1.0.0";
    coop::WriteLog(root, L"patch.log", message.str());

    if (!coop::th12::IsTh12Process()) {
        coop::WriteLog(root, L"patch.log", L"phase2 address validation skipped outside th12.exe");
        return;
    }
    std::wstring error;
    if (coop::th12::ValidateAddressMap(error)) {
        if (!coop::th12::InitializeRuntimeDialogueSkip(root, error)) {
            coop::WriteLog(root, L"patch.log",
                           L"phase13 dialogue auto-skip initialization failed: " +
                               error);
            error.clear();
        }
        if (coop::th12::InitializeRuntimePlayerContexts(error)) {
            coop::WriteLog(root, L"patch.log",
                           L"phase3 P1 context captured read-only; airframes=6");
            if (!coop::th12::InitializeRuntimeFrameInput(root, error)) {
                coop::WriteLog(root, L"patch.log",
                               L"phase13 initialization failed; continuing with local input: " +
                                   error);
                error.clear();
            }
            if (!coop::th12::InitializeRuntimeDeterminism(root, error)) {
                coop::WriteLog(root, L"patch.log",
                               L"phase7 initialization failed; trace disabled: " +
                                   error);
                error.clear();
            }
            if (!coop::th12::InitializeRuntimeResourceTransactions(root, error)) {
                coop::WriteLog(root, L"patch.log",
                               L"phase11 initialization failed; native resource writes retained: " +
                                   error);
                error.clear();
            }
            if (coop::th12::InitializeRuntimeBosses(root, error)) {
                coop::WriteLog(root, L"patch.log",
                               L"phase12 all Boss effective HP scaling armed at 150 percent; native HP thresholds retained");
            } else {
                coop::WriteLog(root, L"patch.log",
                               L"phase12 initialization failed; native Boss damage retained: " +
                                   error);
                error.clear();
            }
            if (!coop::th12::InitializeRuntimeBombs(root, error)) {
                coop::WriteLog(root, L"patch.log",
                               L"phase10 initialization failed; P2 Bomb disabled: " +
                                   error);
                error.clear();
            }
            if (coop::th12::InitializeRuntimeResources(root, error)) {
                coop::WriteLog(root, L"patch.log",
                               L"phase4 resource lifecycle hooks installed");
                if (coop::th12::InitializeRuntimePlayer2(root, error)) {
                    coop::WriteLog(root, L"patch.log",
                                   L"phase5 P2 movement lifecycle armed");
                    if (coop::th12::InitializeRuntimeCollisions(root, error)) {
                        coop::WriteLog(root, L"patch.log",
                                       L"phase9 collision, damage and item ownership hooks installed");
                    } else {
                        coop::WriteLog(root, L"patch.log",
                                       L"phase9 initialization failed; P2 remains non-colliding: " +
                                           error);
                        error.clear();
                    }
                } else {
                    coop::WriteLog(root, L"patch.log",
                                   L"phase5 initialization failed; continuing without P2: " +
                                       error);
                }
            } else {
                coop::WriteLog(root, L"patch.log",
                               L"phase4 initialization failed; continuing single-player: " +
                                   error);
            }
        } else {
            coop::WriteLog(root, L"patch.log", L"phase3 context initialization failed: " + error);
        }
    } else {
        coop::WriteLog(root, L"patch.log", L"address map rejected: " + error);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
