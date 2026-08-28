#include "log.h"
#include "path_util.h"

#include <windows.h>
#include <dinput.h>
#include <string>

namespace {

HMODULE g_realDinput8 = nullptr;
HRESULT (WINAPI* g_directInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN) = nullptr;
INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK InitializeProxy(PINIT_ONCE, PVOID, PVOID*) {
    const std::wstring root = coop::ModuleDirectory(nullptr);
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        coop::WriteLog(root, L"bootstrap.log", L"GetSystemDirectoryW failed");
        return FALSE;
    }
    const std::wstring realPath = coop::JoinPath(systemDirectory, L"dinput8.dll");
    g_realDinput8 = LoadLibraryW(realPath.c_str());
    if (g_realDinput8 == nullptr) {
        coop::WriteLog(root, L"bootstrap.log", L"Unable to load system dinput8.dll");
        return FALSE;
    }
    g_directInput8Create = reinterpret_cast<decltype(g_directInput8Create)>(
        GetProcAddress(g_realDinput8, "DirectInput8Create"));
    if (g_directInput8Create == nullptr) {
        coop::WriteLog(root, L"bootstrap.log", L"DirectInput8Create export not found");
        return FALSE;
    }

    const std::wstring configPath = coop::JoinPath(root, L"coop\\config.ini");
    const bool patchEnabled = GetPrivateProfileIntW(L"patch", L"enabled", 1, configPath.c_str()) != 0;
    if (patchEnabled) {
        const std::wstring patchPath = coop::JoinPath(root, L"th12_coop.dll");
        HMODULE patch = LoadLibraryW(patchPath.c_str());
        if (patch != nullptr) {
            using InitializePatch = void (WINAPI*)();
            const auto initialize = reinterpret_cast<InitializePatch>(
                GetProcAddress(patch, "Th12CoopInitialize"));
            if (initialize != nullptr) {
                initialize();
            } else {
                coop::WriteLog(root, L"bootstrap.log", L"th12_coop.dll has no initializer");
            }
        } else {
            coop::WriteLog(root, L"bootstrap.log", L"th12_coop.dll not found; continuing unpatched");
        }
    } else {
        coop::WriteLog(root, L"bootstrap.log", L"patch disabled by coop\\config.ini");
    }
    coop::WriteLog(root, L"bootstrap.log", L"dinput8 proxy initialized");
    return TRUE;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version,
                                               REFIID interfaceId, LPVOID* out,
                                               LPUNKNOWN outer) {
    if (!InitOnceExecuteOnce(&g_initOnce, InitializeProxy, nullptr, nullptr) ||
        g_directInput8Create == nullptr) {
        return E_FAIL;
    }
    return g_directInput8Create(instance, version, interfaceId, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
