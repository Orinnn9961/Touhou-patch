#include <windows.h>
#include <dinput.h>
#include <iostream>

namespace {
const GUID kDirectInput8W =
    {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* path = argc > 1 ? argv[1] : L"dinput8.dll";
    HMODULE proxy = LoadLibraryW(path);
    if (proxy == nullptr) {
        std::wcerr << L"LoadLibrary failed: " << GetLastError() << L"\n";
        return 1;
    }
    using DirectInput8CreateFn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto create = reinterpret_cast<DirectInput8CreateFn>(
        GetProcAddress(proxy, "DirectInput8Create"));
    if (create == nullptr) {
        std::wcerr << L"DirectInput8Create export missing\n";
        FreeLibrary(proxy);
        return 2;
    }
    void* directInput = nullptr;
    const HRESULT result = create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                  kDirectInput8W, &directInput, nullptr);
    if (FAILED(result) || directInput == nullptr) {
        std::wcerr << L"DirectInput8Create failed: 0x" << std::hex << result << L"\n";
        FreeLibrary(proxy);
        return 3;
    }
    reinterpret_cast<IUnknown*>(directInput)->Release();
    FreeLibrary(proxy);
    std::wcout << L"proxy smoke test passed\n";
    return 0;
}
