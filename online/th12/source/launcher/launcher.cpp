#include "log.h"
#include "path_util.h"
#include "resource.h"
#include "sha256.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <ws2tcpip.h>

#include <array>
#include <algorithm>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kExpectedExeSha256[] =
    L"D8D644D2E64957A3031B1A1399D0502E1DDAA5252D2C4E492770AD6717827628";

std::wstring g_root;

enum class NetworkRole {
    kLocal,
    kHost,
    kClient,
};

struct LaunchOptions {
    NetworkRole role{NetworkRole::kLocal};
    std::wstring peerAddress{L"127.0.0.1"};
    uint32_t room{12012};
    uint16_t port{28765};
    uint8_t delay{2};
    uint8_t redundancy{8};
    std::wstring player2Airframe{L"sanae_b"};
    bool lockPower{};
    bool lockLives{};
    bool autoBomb{};
    bool infiniteRespawn{};
    bool launch{true};
    bool configureOnly{};
    bool silent{};
    bool showHelp{};
};

int Run(const LaunchOptions& options);

constexpr std::array<const wchar_t*, 6> kAirframeIds{
    L"reimu_a", L"reimu_b", L"marisa_a",
    L"marisa_b", L"sanae_a", L"sanae_b",
};

constexpr std::array<const wchar_t*, 6> kAirframeLabels{
    L"\x7075\x68a6 A", L"\x7075\x68a6 B", L"\x9b54\x7406\x6c99 A",
    L"\x9b54\x7406\x6c99 B", L"\x65e9\x82d7 A", L"\x65e9\x82d7 B",
};

enum ControlId : int {
    kModeLocal = 100,
    kModeHost,
    kModeClient,
    kPeerAddress,
    kRoom,
    kPort,
    kDelay,
    kRedundancy,
    kPlayer2,
    kLockPower,
    kLockLives,
    kAutoBomb,
    kInfiniteRespawn,
    kSave,
    kStart,
    kStop,
};

struct GuiControls {
    HWND window{};
    HWND modeLocal{};
    HWND modeHost{};
    HWND modeClient{};
    HWND peerLabel{};
    HWND peerAddress{};
    HWND roomLabel{};
    HWND room{};
    HWND portLabel{};
    HWND port{};
    HWND delayLabel{};
    HWND delay{};
    HWND redundancyLabel{};
    HWND redundancy{};
    HWND player2Group{};
    HWND player2Label{};
    HWND player2{};
    HWND hostRulesGroup{};
    HWND lockPower{};
    HWND lockLives{};
    HWND autoBomb{};
    HWND infiniteRespawn{};
    HWND status{};
    HWND stop{};
    HFONT normalFont{};
    HFONT titleFont{};
};

GuiControls g_gui;
HANDLE g_guiGameProcess = nullptr;
bool g_guiNetworkSession = false;
bool g_guiStopRequested = false;
uint64_t g_guiStopDeadline = 0;
constexpr UINT kLobbyTimer = 17;

bool Is32BitPe(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    DWORD read = 0;
    const bool success = ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
                         read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                         SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
                         ReadFile(file, &nt, sizeof(nt), &read, nullptr) &&
                         read >= sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) &&
                         nt.Signature == IMAGE_NT_SIGNATURE &&
                         nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
    CloseHandle(file);
    return success;
}

bool VerifyFile(const wchar_t* name, bool require32Bit, std::wstring& error) {
    const std::wstring path = coop::JoinPath(g_root, name);
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        error = std::wstring(L"Missing file: ") + name;
        return false;
    }
    if (require32Bit && !Is32BitPe(path)) {
        error = std::wstring(L"Not a 32-bit PE file: ") + name;
        return false;
    }
    return true;
}

bool VerifyInstallation(std::wstring& error, std::wstring& exeHash) {
    if (!VerifyFile(L"th12.exe", true, error)) {
        return false;
    }
    if (!VerifyFile(L"dinput8.dll", true, error) ||
        !VerifyFile(L"th12_coop.dll", true, error)) {
        return false;
    }
    if (!coop::Sha256File(coop::JoinPath(g_root, L"th12.exe"), exeHash)) {
        error = L"Unable to calculate th12.exe SHA-256";
        return false;
    }
    if (exeHash != kExpectedExeSha256) {
        error = L"Unsupported th12.exe build (expected Japanese 1.00b)";
        return false;
    }
    return true;
}

void ShowResult(const std::wstring& title, const std::wstring& text, UINT type) {
    MessageBoxW(nullptr, text.c_str(), title.c_str(), type | MB_SETFOREGROUND);
}

bool IsAirframe(const std::wstring& value) {
    for (const wchar_t* id : kAirframeIds) {
        if (_wcsicmp(value.c_str(), id) == 0) {
            return true;
        }
    }
    return false;
}

bool ParseNumber(const wchar_t* text, uint32_t minimum, uint32_t maximum,
                 uint32_t& value) {
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text, &end, 10);
    if (text == end || *end != L'\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseArguments(LaunchOptions& options, std::wstring& error) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        error = L"Unable to read launcher arguments.";
        return false;
    }
    bool valid = true;
    for (int i = 1; i < count && valid; ++i) {
        const std::wstring argument = arguments[i];
        auto requireValue = [&]() -> const wchar_t* {
            if (i + 1 >= count) {
                valid = false;
                error = L"Missing value after " + argument;
                return nullptr;
            }
            return arguments[++i];
        };
        if (argument == L"--host") {
            options.role = NetworkRole::kHost;
        } else if (argument == L"--join") {
            options.role = NetworkRole::kClient;
            const wchar_t* value = requireValue();
            if (value != nullptr) {
                options.peerAddress = value;
            }
        } else if (argument == L"--local") {
            options.role = NetworkRole::kLocal;
        } else if (argument == L"--verify") {
            options.launch = false;
        } else if (argument == L"--configure-only") {
            options.launch = false;
            options.configureOnly = true;
        } else if (argument == L"--silent") {
            options.silent = true;
        } else if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
            options.launch = false;
        } else if (argument == L"--lock-power") {
            options.lockPower = true;
        } else if (argument == L"--lock-lives") {
            options.lockLives = true;
        } else if (argument == L"--auto-bomb") {
            options.autoBomb = true;
        } else if (argument == L"--infinite-respawn") {
            options.infiniteRespawn = true;
        } else if (argument == L"--p1" || argument == L"--p2") {
            const wchar_t* value = requireValue();
            if (value != nullptr && !IsAirframe(value)) {
                valid = false;
                error = L"Unknown airframe: " + std::wstring(value);
            } else if (value != nullptr) {
                // P1 is selected in game. Keep accepting the old --p1
                // switch so existing shortcuts remain usable.
                if (argument != L"--p1") {
                    options.player2Airframe = value;
                }
            }
        } else if (argument == L"--port" || argument == L"--room" ||
                   argument == L"--delay" || argument == L"--redundancy") {
            const wchar_t* valueText = requireValue();
            uint32_t value = 0;
            uint32_t minimum = 1;
            uint32_t maximum = UINT16_MAX;
            if (argument == L"--room") {
                maximum = INT32_MAX;
            } else if (argument == L"--delay") {
                minimum = 0;
                maximum = 12;
            } else if (argument == L"--redundancy") {
                maximum = 16;
            }
            if (valueText != nullptr &&
                !ParseNumber(valueText, minimum, maximum, value)) {
                valid = false;
                error = L"Invalid value for " + argument;
            } else if (valueText != nullptr && argument == L"--port") {
                options.port = static_cast<uint16_t>(value);
            } else if (valueText != nullptr && argument == L"--room") {
                options.room = value;
            } else if (valueText != nullptr && argument == L"--delay") {
                options.delay = static_cast<uint8_t>(value);
            } else if (valueText != nullptr) {
                options.redundancy = static_cast<uint8_t>(value);
            }
        } else {
            valid = false;
            error = L"Unknown launcher argument: " + argument;
        }
    }
    LocalFree(arguments);
    if (valid && options.role == NetworkRole::kClient &&
        options.peerAddress.empty()) {
        error = L"Client mode requires the host IPv4 address.";
        return false;
    }
    return valid;
}

bool WriteSetting(const std::wstring& path, const wchar_t* section,
                  const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(section, key, value.c_str(),
                                      path.c_str()) != 0;
}

bool ConfigureSession(const LaunchOptions& options, std::wstring& error) {
    const std::wstring path = coop::JoinPath(g_root, L"coop\\config.ini");
    const wchar_t* mode = options.role == NetworkRole::kHost
        ? L"host"
        : options.role == NetworkRole::kClient ? L"client" : L"off";
    const std::wstring listenPort = options.role == NetworkRole::kClient
        ? L"0"
        : std::to_wstring(options.port);
    if (!WriteSetting(path, L"phase6", L"network_mode", mode) ||
        !WriteSetting(path, L"phase6", L"listen_port", listenPort) ||
        !WriteSetting(path, L"phase6", L"peer_address",
                      options.peerAddress) ||
        !WriteSetting(path, L"phase6", L"peer_port",
                      std::to_wstring(options.port)) ||
        !WriteSetting(path, L"phase6", L"session_id",
                      std::to_wstring(options.room)) ||
        !WriteSetting(path, L"phase6", L"input_delay",
                      std::to_wstring(options.delay)) ||
        !WriteSetting(path, L"phase6", L"input_redundancy",
                      std::to_wstring(options.redundancy)) ||
        !WriteSetting(path, L"coop_rules", L"lock_power",
                      options.lockPower ? L"1" : L"0") ||
        !WriteSetting(path, L"coop_rules", L"lock_lives",
                      options.lockLives ? L"1" : L"0") ||
        !WriteSetting(path, L"coop_rules", L"auto_bomb",
                      options.autoBomb ? L"1" : L"0") ||
        !WriteSetting(path, L"coop_rules", L"infinite_respawn",
                      options.infiniteRespawn ? L"1" : L"0")) {
        error = L"Unable to update coop\\config.ini.";
        return false;
    }
    if (options.role == NetworkRole::kLocal &&
        !WriteSetting(path, L"phase4", L"player2_airframe",
                      options.player2Airframe)) {
        error = L"Unable to update coop\\config.ini.";
        return false;
    }
    WritePrivateProfileStringW(L"phase4", L"player1_airframe", nullptr,
                               path.c_str());
    return true;
}

uint32_t ReadSettingNumber(const std::wstring& path, const wchar_t* section,
                           const wchar_t* key, uint32_t fallback,
                           uint32_t minimum, uint32_t maximum) {
    const UINT value = GetPrivateProfileIntW(section, key, fallback, path.c_str());
    return std::clamp(static_cast<uint32_t>(value), minimum, maximum);
}

void ReadSettingText(const std::wstring& path, const wchar_t* section,
                     const wchar_t* key, const wchar_t* fallback,
                     std::wstring& value) {
    wchar_t buffer[128]{};
    GetPrivateProfileStringW(section, key, fallback, buffer,
                             static_cast<DWORD>(std::size(buffer)), path.c_str());
    value = buffer;
}

void LoadSessionConfiguration(LaunchOptions& options) {
    const std::wstring path = coop::JoinPath(g_root, L"coop\\config.ini");
    std::wstring mode;
    ReadSettingText(path, L"phase6", L"network_mode", L"off", mode);
    if (_wcsicmp(mode.c_str(), L"host") == 0) {
        options.role = NetworkRole::kHost;
    } else if (_wcsicmp(mode.c_str(), L"client") == 0) {
        options.role = NetworkRole::kClient;
    }
    ReadSettingText(path, L"phase6", L"peer_address", L"127.0.0.1",
                    options.peerAddress);
    options.room = ReadSettingNumber(path, L"phase6", L"session_id", 12012,
                                     1, INT32_MAX);
    options.port = static_cast<uint16_t>(ReadSettingNumber(
        path, L"phase6", L"peer_port", 28765, 1, UINT16_MAX));
    options.delay = static_cast<uint8_t>(ReadSettingNumber(
        path, L"phase6", L"input_delay", 2, 0, 12));
    options.redundancy = static_cast<uint8_t>(ReadSettingNumber(
        path, L"phase6", L"input_redundancy", 8, 1, 16));
    options.lockPower = ReadSettingNumber(
        path, L"coop_rules", L"lock_power", 0, 0, 1) != 0;
    options.lockLives = ReadSettingNumber(
        path, L"coop_rules", L"lock_lives", 0, 0, 1) != 0;
    options.autoBomb = ReadSettingNumber(
        path, L"coop_rules", L"auto_bomb", 0, 0, 1) != 0;
    options.infiniteRespawn = ReadSettingNumber(
        path, L"coop_rules", L"infinite_respawn", 0, 0, 1) != 0;
    ReadSettingText(path, L"phase4", L"player2_airframe", L"sanae_b",
                    options.player2Airframe);
    if (!IsAirframe(options.player2Airframe)) {
        options.player2Airframe = L"sanae_b";
    }
}

HWND CreateControl(DWORD extendedStyle, const wchar_t* className,
                   const wchar_t* text, DWORD style, int x, int y,
                   int width, int height, HWND parent, int id) {
    HWND control = CreateWindowExW(
        extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control != nullptr && g_gui.normalFont != nullptr) {
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_gui.normalFont), TRUE);
    }
    return control;
}

void SetNumericText(HWND control, uint32_t value) {
    const std::wstring text = std::to_wstring(value);
    SetWindowTextW(control, text.c_str());
}

std::wstring GetControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

int AirframeIndex(const std::wstring& airframe) {
    for (size_t index = 0; index < kAirframeIds.size(); ++index) {
        if (_wcsicmp(airframe.c_str(), kAirframeIds[index]) == 0) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

void FillAirframeCombo(HWND combo, const std::wstring& selected) {
    for (const wchar_t* label : kAirframeLabels) {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label));
    }
    SendMessageW(combo, CB_SETCURSEL,
                 static_cast<WPARAM>(AirframeIndex(selected)), 0);
}

void SetGuiStatus(const std::wstring& message) {
    SetWindowTextW(g_gui.status, message.c_str());
}

NetworkRole SelectedGuiRole() {
    if (SendMessageW(g_gui.modeHost, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return NetworkRole::kHost;
    }
    if (SendMessageW(g_gui.modeClient, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return NetworkRole::kClient;
    }
    return NetworkRole::kLocal;
}

void UpdateModeControls() {
    const NetworkRole role = SelectedGuiRole();
    const BOOL networkEnabled = role == NetworkRole::kLocal ? FALSE : TRUE;
    const BOOL peerEnabled = role == NetworkRole::kClient ? TRUE : FALSE;
    EnableWindow(g_gui.peerLabel, peerEnabled);
    EnableWindow(g_gui.peerAddress, peerEnabled);
    EnableWindow(g_gui.roomLabel, networkEnabled);
    EnableWindow(g_gui.room, networkEnabled);
    EnableWindow(g_gui.portLabel, networkEnabled);
    EnableWindow(g_gui.port, networkEnabled);
    EnableWindow(g_gui.delayLabel, networkEnabled);
    EnableWindow(g_gui.delay, networkEnabled);
    EnableWindow(g_gui.redundancyLabel, networkEnabled);
    EnableWindow(g_gui.redundancy, networkEnabled);
    const BOOL localMode = role == NetworkRole::kLocal ? TRUE : FALSE;
    ShowWindow(g_gui.player2Group, localMode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.player2Label, localMode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.player2, localMode ? SW_SHOW : SW_HIDE);
    EnableWindow(g_gui.player2Label, localMode);
    EnableWindow(g_gui.player2, localMode);
    const BOOL hostMode = role == NetworkRole::kHost ? TRUE : FALSE;
    ShowWindow(g_gui.hostRulesGroup, hostMode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.lockPower, hostMode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.lockLives, hostMode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.autoBomb, hostMode ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.infiniteRespawn, hostMode ? SW_SHOW : SW_HIDE);
    EnableWindow(g_gui.lockPower, hostMode);
    EnableWindow(g_gui.lockLives, hostMode);
    EnableWindow(g_gui.autoBomb, hostMode);
    EnableWindow(g_gui.infiniteRespawn, hostMode);

    if (role == NetworkRole::kHost) {
        SetGuiStatus(L"\x4e3b\x673a\x6a21\x5f0f");
    } else if (role == NetworkRole::kClient) {
        SetGuiStatus(L"\x5ba2\x6237\x7aef\x6a21\x5f0f");
    } else {
        SetGuiStatus(L"\x672c\x5730\x6a21\x5f0f");
    }
}

bool IsValidIpv4(const std::wstring& address) {
    IN_ADDR parsed{};
    return InetPtonW(AF_INET, address.c_str(), &parsed) == 1;
}

bool ReadGuiOptions(LaunchOptions& options, std::wstring& error) {
    options.role = SelectedGuiRole();
    options.peerAddress = GetControlText(g_gui.peerAddress);
    if (options.role == NetworkRole::kClient &&
        !IsValidIpv4(options.peerAddress)) {
        error = L"\x8bf7\x8f93\x5165\x6709\x6548\x7684\x4e3b\x673a IPv4 \x5730\x5740\x3002";
        return false;
    }

    uint32_t room = 0;
    uint32_t port = 0;
    uint32_t delay = 0;
    uint32_t redundancy = 0;
    const std::wstring roomText = GetControlText(g_gui.room);
    const std::wstring portText = GetControlText(g_gui.port);
    const std::wstring delayText = GetControlText(g_gui.delay);
    const std::wstring redundancyText = GetControlText(g_gui.redundancy);
    if (!ParseNumber(roomText.c_str(), 1, INT32_MAX, room)) {
        error = L"\x623f\x95f4\x53f7\x5fc5\x987b\x4e3a 1 \x81f3 2147483647 \x7684\x6574\x6570\x3002";
        return false;
    }
    if (!ParseNumber(portText.c_str(), 1, UINT16_MAX, port)) {
        error = L"UDP \x7aef\x53e3\x5fc5\x987b\x4e3a 1 \x81f3 65535 \x7684\x6574\x6570\x3002";
        return false;
    }
    if (!ParseNumber(delayText.c_str(), 0, 12, delay)) {
        error = L"\x8f93\x5165\x5ef6\x8fdf\x5fc5\x987b\x4e3a 0 \x81f3 12 \x5e27\x3002";
        return false;
    }
    if (!ParseNumber(redundancyText.c_str(), 1, 16, redundancy)) {
        error = L"\x8f93\x5165\x5197\x4f59\x5fc5\x987b\x4e3a 1 \x81f3 16 \x5e27\x3002";
        return false;
    }
    options.room = room;
    options.port = static_cast<uint16_t>(port);
    options.delay = static_cast<uint8_t>(delay);
    options.redundancy = static_cast<uint8_t>(redundancy);
    options.lockPower =
        SendMessageW(g_gui.lockPower, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.lockLives =
        SendMessageW(g_gui.lockLives, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.autoBomb =
        SendMessageW(g_gui.autoBomb, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.infiniteRespawn =
        SendMessageW(g_gui.infiniteRespawn, BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (options.role == NetworkRole::kLocal) {
        const LRESULT player2Index =
            SendMessageW(g_gui.player2, CB_GETCURSEL, 0, 0);
        if (player2Index < 0 ||
            player2Index >= static_cast<LRESULT>(kAirframeIds.size())) {
            error = L"\x8bf7\x4e3a P2 \x9009\x62e9\x673a\x4f53\x3002";
            return false;
        }
        options.player2Airframe =
            kAirframeIds[static_cast<size_t>(player2Index)];
    }
    return true;
}

std::wstring ReadLobbyState() {
    const std::wstring path = coop::JoinPath(g_root,
                                             L"coop\\lobby-status.ini");
    wchar_t state[64]{};
    wchar_t processId[32]{};
    GetPrivateProfileStringW(L"lobby", L"state", L"starting", state,
                             static_cast<DWORD>(std::size(state)),
                             path.c_str());
    GetPrivateProfileStringW(L"lobby", L"process_id", L"0", processId,
                             static_cast<DWORD>(std::size(processId)),
                             path.c_str());
    if (g_guiGameProcess != nullptr &&
        _wtoi(processId) != static_cast<int>(GetProcessId(g_guiGameProcess))) {
        return L"starting";
    }
    return state;
}

void UpdateLobbyStatus() {
    const std::wstring state = ReadLobbyState();
    if (state == L"waiting_peer") {
        SetGuiStatus(L"\x7b49\x5f85\x5bf9\x65b9\x52a0\x5165");
    } else if (state == L"loading_resources") {
        SetGuiStatus(L"\x53cc\x65b9\x6b63\x5728\x52a0\x8f7d\x8d44\x6e90");
    } else if (state == L"ready") {
        SetGuiStatus(L"\x53cc\x65b9\x5df2\x51c6\x5907");
    } else if (state == L"waiting_start") {
        SetGuiStatus(L"\x7b49\x5f85\x53cc\x65b9\x540c\x6b65\x5f00\x59cb");
    } else if (state == L"configuration_mismatch") {
        SetGuiStatus(L"\x914d\x7f6e\x4e0d\x4e00\x81f4");
    } else if (state == L"resource_error") {
        SetGuiStatus(L"\x8d44\x6e90\x52a0\x8f7d\x5931\x8d25");
    } else if (state == L"network_error") {
        SetGuiStatus(L"\x8054\x673a\x4f1a\x8bdd\x5df2\x7ec8\x6b62");
    } else if (state == L"running") {
        SetGuiStatus(L"\x8054\x673a\x5df2\x5f00\x59cb");
    } else {
        SetGuiStatus(L"\x6e38\x620f\x5df2\x542f\x52a8");
    }
}

void ShowGuiError(const std::wstring& error) {
    MessageBoxW(g_gui.window, error.c_str(), L"TH12 Co-op Launcher",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void SetGuiSessionBusy(bool busy) {
    const BOOL enabled = busy ? FALSE : TRUE;
    EnableWindow(g_gui.modeLocal, enabled);
    EnableWindow(g_gui.modeHost, enabled);
    EnableWindow(g_gui.modeClient, enabled);
    EnableWindow(GetDlgItem(g_gui.window, kSave), enabled);
    EnableWindow(GetDlgItem(g_gui.window, kStart), enabled);
    EnableWindow(g_gui.stop,
                 busy && g_guiGameProcess != nullptr && !g_guiStopRequested);
    if (!busy) {
        UpdateModeControls();
    } else {
        EnableWindow(g_gui.peerAddress, FALSE);
        EnableWindow(g_gui.room, FALSE);
        EnableWindow(g_gui.port, FALSE);
        EnableWindow(g_gui.delay, FALSE);
        EnableWindow(g_gui.redundancy, FALSE);
        EnableWindow(g_gui.lockPower, FALSE);
        EnableWindow(g_gui.lockLives, FALSE);
        EnableWindow(g_gui.autoBomb, FALSE);
        EnableWindow(g_gui.infiniteRespawn, FALSE);
    }
}

struct CloseWindowRequest {
    DWORD processId{};
    bool posted{};
};

BOOL CALLBACK PostCloseToGameWindow(HWND window, LPARAM parameter) {
    auto* request = reinterpret_cast<CloseWindowRequest*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == request->processId &&
        PostMessageW(window, WM_CLOSE, 0, 0) != FALSE) {
        request->posted = true;
    }
    return TRUE;
}

void RequestGuiGameStop() {
    if (g_guiGameProcess == nullptr || g_guiStopRequested) {
        return;
    }
    g_guiStopRequested = true;
    g_guiStopDeadline = GetTickCount64() + 3000;
    EnableWindow(g_gui.stop, FALSE);
    SetGuiStatus(L"\x6b63\x5728\x5173\x95ed\x6e38\x620f");

    CloseWindowRequest request{};
    request.processId = GetProcessId(g_guiGameProcess);
    EnumWindows(PostCloseToGameWindow,
                reinterpret_cast<LPARAM>(&request));
    coop::WriteLog(g_root, L"launcher.log",
                   request.posted
                       ? L"game close requested through WM_CLOSE; pid=" +
                             std::to_wstring(request.processId)
                       : L"no game window found; forced close scheduled; pid=" +
                             std::to_wstring(request.processId));
    if (!request.posted) {
        g_guiStopDeadline = GetTickCount64();
    }
}

void HandleGuiAction(bool launch) {
    LaunchOptions options{};
    std::wstring error;
    if (!ReadGuiOptions(options, error)) {
        ShowGuiError(error);
        return;
    }
    std::wstring hash;
    if (!VerifyInstallation(error, hash)) {
        ShowGuiError(error);
        return;
    }
    if (!ConfigureSession(options, error)) {
        ShowGuiError(error);
        return;
    }
    if (!launch) {
        SetGuiStatus(L"\x914d\x7f6e\x5df2\x4fdd\x5b58\x3002");
        return;
    }
    g_guiNetworkSession = options.role != NetworkRole::kLocal;
    if (Run(options) == 0) {
        if (g_guiNetworkSession) {
            SetGuiSessionBusy(true);
            SetGuiStatus(L"\x6e38\x620f\x5df2\x542f\x52a8");
            SetTimer(g_gui.window, kLobbyTimer, 500, nullptr);
        } else {
            DestroyWindow(g_gui.window);
        }
    }
}

void CreateNumberSpinner(HWND parent, HWND buddy, int minimum, int maximum) {
    HWND spinner = CreateWindowExW(
        0, UPDOWN_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(spinner, UDM_SETBUDDY, reinterpret_cast<WPARAM>(buddy), 0);
    SendMessageW(spinner, UDM_SETRANGE32, static_cast<WPARAM>(minimum),
                 static_cast<LPARAM>(maximum));
}

LRESULT CALLBACK LauncherWindowProc(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_gui.window = window;
        g_gui.normalFont = CreateFontW(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_gui.titleFont = CreateFontW(
            -24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HWND title = CreateControl(0, L"STATIC", L"\x4e1c\x65b9\x661f\x83b2\x8239 \x53cc\x4eba\x8054\x673a",
                                   SS_LEFT, 24, 18, 550, 34, window, 0);
        SendMessageW(title, WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_gui.titleFont), TRUE);
        g_gui.status = CreateControl(0, L"STATIC", L"", SS_LEFT,
                                     24, 54, 550, 24, window, 0);

        CreateControl(0, L"BUTTON", L"\x8fd0\x884c\x6a21\x5f0f", BS_GROUPBOX,
                      24, 88, 552, 196, window, 0);
        g_gui.modeLocal = CreateControl(0, L"BUTTON", L"\x672c\x5730\x53cc\x4eba",
                                        BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
                                        44, 116, 128, 24, window, kModeLocal);
        g_gui.modeHost = CreateControl(0, L"BUTTON", L"\x521b\x5efa\x623f\x95f4 (P1)",
                                       BS_AUTORADIOBUTTON | WS_TABSTOP,
                                       190, 116, 156, 24, window, kModeHost);
        g_gui.modeClient = CreateControl(0, L"BUTTON", L"\x52a0\x5165\x623f\x95f4 (P2)",
                                         BS_AUTORADIOBUTTON | WS_TABSTOP,
                                         365, 116, 166, 24, window, kModeClient);

        g_gui.peerLabel = CreateControl(0, L"STATIC", L"\x4e3b\x673a IPv4", SS_LEFT,
                                        44, 153, 90, 24, window, 0);
        g_gui.peerAddress = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          ES_AUTOHSCROLL | WS_TABSTOP,
                                          142, 150, 185, 26, window, kPeerAddress);

        g_gui.roomLabel = CreateControl(0, L"STATIC", L"\x623f\x95f4\x53f7", SS_LEFT,
                                        44, 192, 80, 24, window, 0);
        g_gui.room = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                                   142, 189, 140, 26, window, kRoom);
        g_gui.portLabel = CreateControl(0, L"STATIC", L"UDP \x7aef\x53e3", SS_LEFT,
                                        315, 192, 88, 24, window, 0);
        g_gui.port = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                                   411, 189, 120, 26, window, kPort);

        g_gui.delayLabel = CreateControl(0, L"STATIC", L"\x5ef6\x8fdf\x5e27", SS_LEFT,
                                         44, 231, 80, 24, window, 0);
        g_gui.delay = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                                    142, 228, 140, 26, window, kDelay);
        g_gui.redundancyLabel = CreateControl(0, L"STATIC", L"\x5197\x4f59\x5e27", SS_LEFT,
                                              315, 231, 88, 24, window, 0);
        g_gui.redundancy = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                                         411, 228, 120, 26, window, kRedundancy);
        CreateNumberSpinner(window, g_gui.room, 1, INT32_MAX);
        CreateNumberSpinner(window, g_gui.port, 1, UINT16_MAX);
        CreateNumberSpinner(window, g_gui.delay, 0, 12);
        CreateNumberSpinner(window, g_gui.redundancy, 1, 16);

        g_gui.player2Group = CreateControl(
            0, L"BUTTON", L"P2 \x673a\x4f53", BS_GROUPBOX,
            24, 300, 552, 100, window, 0);
        g_gui.player2Label = CreateControl(0, L"STATIC", L"P2", SS_LEFT,
                                           92, 332, 32, 24, window, 0);
        g_gui.player2 = CreateControl(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                      CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                      132, 328, 310, 180, window, kPlayer2);

        g_gui.hostRulesGroup = CreateControl(
            0, L"BUTTON", L"\x4e3b\x673a\x6e38\x620f\x89c4\x5219", BS_GROUPBOX,
            24, 300, 552, 100, window, 0);
        g_gui.lockPower = CreateControl(
            0, L"BUTTON", L"\x9501\x706b\x529b",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            62, 324, 180, 28, window, kLockPower);
        g_gui.lockLives = CreateControl(
            0, L"BUTTON", L"\x9501\x6b8b\x673a",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            300, 324, 180, 28, window, kLockLives);
        g_gui.autoBomb = CreateControl(
            0, L"BUTTON", L"\x81ea\x52a8 Bomb",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            62, 358, 180, 28, window, kAutoBomb);
        g_gui.infiniteRespawn = CreateControl(
            0, L"BUTTON", L"\x65e0\x9650\x590d\x6d3b",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            300, 358, 180, 28, window, kInfiniteRespawn);

        g_gui.stop = CreateControl(
            0, L"BUTTON", L"\x5173\x95ed\x6e38\x620f",
            BS_PUSHBUTTON | WS_TABSTOP,
            196, 420, 122, 34, window, kStop);
        EnableWindow(g_gui.stop, FALSE);
        CreateControl(0, L"BUTTON", L"\x4fdd\x5b58\x914d\x7f6e",
                      BS_PUSHBUTTON | WS_TABSTOP,
                      330, 420, 110, 34, window, kSave);
        HWND start = CreateControl(0, L"BUTTON", L"\x542f\x52a8\x6e38\x620f",
                                   BS_DEFPUSHBUTTON | WS_TABSTOP,
                                   452, 420, 124, 34, window, kStart);
        SendMessageW(window, DM_SETDEFID, kStart, 0);
        SetFocus(start);

        LaunchOptions options{};
        LoadSessionConfiguration(options);
        SetWindowTextW(g_gui.peerAddress, options.peerAddress.c_str());
        SetNumericText(g_gui.room, options.room);
        SetNumericText(g_gui.port, options.port);
        SetNumericText(g_gui.delay, options.delay);
        SetNumericText(g_gui.redundancy, options.redundancy);
        FillAirframeCombo(g_gui.player2, options.player2Airframe);
        SendMessageW(g_gui.lockPower, BM_SETCHECK,
                     options.lockPower ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_gui.lockLives, BM_SETCHECK,
                     options.lockLives ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_gui.autoBomb, BM_SETCHECK,
                     options.autoBomb ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_gui.infiniteRespawn, BM_SETCHECK,
                     options.infiniteRespawn ? BST_CHECKED : BST_UNCHECKED, 0);
        const int roleControl = options.role == NetworkRole::kHost
            ? kModeHost
            : options.role == NetworkRole::kClient ? kModeClient : kModeLocal;
        CheckRadioButton(window, kModeLocal, kModeClient, roleControl);
        UpdateModeControls();
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case kModeLocal:
            case kModeHost:
            case kModeClient:
                UpdateModeControls();
                return 0;
            case kSave:
                HandleGuiAction(false);
                return 0;
            case kStart:
                HandleGuiAction(true);
                return 0;
            case kStop:
                RequestGuiGameStop();
                return 0;
            default:
                break;
            }
        }
        break;
    case WM_TIMER:
        if (wParam == kLobbyTimer && g_guiGameProcess != nullptr) {
            if (WaitForSingleObject(g_guiGameProcess, 0) == WAIT_OBJECT_0) {
                CloseHandle(g_guiGameProcess);
                g_guiGameProcess = nullptr;
                g_guiNetworkSession = false;
                g_guiStopRequested = false;
                g_guiStopDeadline = 0;
                KillTimer(window, kLobbyTimer);
                SetGuiSessionBusy(false);
                SetGuiStatus(L"\x6e38\x620f\x5df2\x9000\x51fa");
            } else {
                if (g_guiStopRequested &&
                    GetTickCount64() >= g_guiStopDeadline) {
                    const DWORD processId = GetProcessId(g_guiGameProcess);
                    if (TerminateProcess(g_guiGameProcess, 0) != FALSE) {
                        coop::WriteLog(
                            g_root, L"launcher.log",
                            L"game did not close within 3 seconds; process terminated; pid=" +
                                std::to_wstring(processId));
                        SetGuiStatus(L"\x6b63\x5728\x7ed3\x675f\x6e38\x620f");
                        g_guiStopDeadline = UINT64_MAX;
                    } else {
                        coop::WriteLog(
                            g_root, L"launcher.log",
                            L"unable to terminate game; Win32 error=" +
                                std::to_wstring(GetLastError()));
                        g_guiStopDeadline = GetTickCount64() + 1000;
                    }
                } else if (!g_guiStopRequested) {
                    UpdateLobbyStatus();
                }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(window, kLobbyTimer);
        if (g_guiGameProcess != nullptr) {
            CloseHandle(g_guiGameProcess);
            g_guiGameProcess = nullptr;
        }
        if (g_gui.normalFont != nullptr) {
            DeleteObject(g_gui.normalFont);
            g_gui.normalFont = nullptr;
        }
        if (g_gui.titleFont != nullptr) {
            DeleteObject(g_gui.titleFont);
            g_gui.titleFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int ShowLauncherGui(HINSTANCE instance, int showCommand) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = LauncherWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_COOP_LAUNCHER), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_COOP_LAUNCHER), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"Th12CoopLauncherWindow";
    if (RegisterClassExW(&windowClass) == 0) {
        ShowResult(L"TH12 Co-op Launcher", L"Unable to create launcher window.",
                   MB_ICONERROR);
        return 5;
    }

    RECT bounds{0, 0, 600, 490};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                               WS_MINIMIZEBOX,
                       FALSE, 0);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND window = CreateWindowExW(
        0, windowClass.lpszClassName, L"TH12 Co-op Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        ShowResult(L"TH12 Co-op Launcher", L"Unable to create launcher window.",
                   MB_ICONERROR);
        return 5;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

bool HasCommandLineArguments() {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return false;
    }
    LocalFree(arguments);
    return count > 1;
}

std::wstring Usage() {
    return L"Local:\n  coop-launcher.exe --local\n\n"
           L"Create LAN room:\n  coop-launcher.exe --host --room 12012 --port 28765\n\n"
           L"Join LAN room:\n  coop-launcher.exe --join 192.168.1.10 --room 12012 --port 28765\n\n"
           L"Local-only P2 airframe: reimu_a, reimu_b, marisa_a, marisa_b, sanae_a, sanae_b\n"
           L"Optional: --delay 6 --redundancy 8 --configure-only\n"
           L"Host rules: --lock-power --lock-lives --auto-bomb --infinite-respawn\n"
           L"Network P1/P2 airframes are selected in the game menus.";
}

int Run(const LaunchOptions& options) {
    std::wstring error;
    std::wstring hash;
    if (!VerifyInstallation(error, hash)) {
        coop::WriteLog(g_root, L"launcher.log", L"verification failed: " + error);
        if (!options.silent) {
            ShowResult(L"TH12 Co-op Launcher", error, MB_ICONERROR);
        }
        return 2;
    }
    coop::WriteLog(g_root, L"launcher.log", L"verification succeeded; th12.exe sha256=" + hash);
    if ((options.launch || options.configureOnly) &&
        !ConfigureSession(options, error)) {
        coop::WriteLog(g_root, L"launcher.log", L"configuration failed: " + error);
        if (!options.silent) {
            ShowResult(L"TH12 Co-op Launcher", error, MB_ICONERROR);
        }
        return 4;
    }
    if (!options.launch) {
        if (!options.silent) {
            const wchar_t* result = options.configureOnly
                ? L"Room configuration saved successfully."
                : L"Verification succeeded.\n\nTH12 Japanese 1.00b detected.";
            ShowResult(L"TH12 Co-op Launcher", result, MB_ICONINFORMATION);
        }
        return 0;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring executable = coop::JoinPath(g_root, L"th12.exe");
    HANDLE gameProcess = nullptr;
    DWORD gameProcessId = 0;
    if (CreateProcessW(executable.c_str(), nullptr, nullptr, nullptr, FALSE, 0,
                       nullptr, g_root.c_str(), &startup, &process)) {
        gameProcess = process.hProcess;
        gameProcessId = process.dwProcessId;
        CloseHandle(process.hThread);
    } else {
        const DWORD createError = GetLastError();
        if (createError == ERROR_ELEVATION_REQUIRED) {
            coop::WriteLog(
                g_root, L"launcher.log",
                L"th12.exe requires elevation; requesting UAC consent");
            SHELLEXECUTEINFOW execute{};
            execute.cbSize = sizeof(execute);
            execute.fMask = SEE_MASK_NOCLOSEPROCESS;
            execute.hwnd = g_gui.window;
            execute.lpVerb = L"runas";
            execute.lpFile = executable.c_str();
            execute.lpDirectory = g_root.c_str();
            execute.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&execute) != FALSE &&
                execute.hProcess != nullptr) {
                gameProcess = execute.hProcess;
                gameProcessId = GetProcessId(gameProcess);
            } else {
                const DWORD elevationError = GetLastError();
                error = elevationError == ERROR_CANCELLED
                    ? L"The UAC elevation request was cancelled. To avoid elevation, clear 'Run this program as an administrator' in th12.exe compatibility properties."
                    : L"Unable to start elevated th12.exe (Win32 error " +
                          std::to_wstring(elevationError) + L")";
            }
        } else {
            error = L"Unable to start th12.exe (Win32 error " +
                    std::to_wstring(createError) + L")";
        }
        if (gameProcess == nullptr) {
            coop::WriteLog(g_root, L"launcher.log", error);
            ShowResult(L"TH12 Co-op Launcher", error, MB_ICONERROR);
            return 3;
        }
    }
    coop::WriteLog(g_root, L"launcher.log",
                   L"started th12.exe; pid=" +
                       std::to_wstring(gameProcessId));
    if (g_guiNetworkSession) {
        g_guiGameProcess = gameProcess;
        const std::wstring statusPath = coop::JoinPath(
            g_root, L"coop\\lobby-status.ini");
        WritePrivateProfileStringW(L"lobby", L"state", L"starting",
                                   statusPath.c_str());
        const std::wstring processId = std::to_wstring(gameProcessId);
        WritePrivateProfileStringW(L"lobby", L"process_id",
                                   processId.c_str(), statusPath.c_str());
    } else {
        CloseHandle(gameProcess);
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_root = coop::ModuleDirectory(nullptr);
    SetProcessDPIAware();
    if (!HasCommandLineArguments()) {
        return ShowLauncherGui(instance, showCommand);
    }
    LaunchOptions options{};
    std::wstring error;
    if (!ParseArguments(options, error)) {
        coop::WriteLog(g_root, L"launcher.log", L"argument error: " + error);
        if (!options.silent) {
            ShowResult(L"TH12 Co-op Launcher", error + L"\n\n" + Usage(),
                       MB_ICONERROR);
        }
        return 1;
    }
    if (options.showHelp) {
        ShowResult(L"TH12 Co-op Launcher", Usage(), MB_ICONINFORMATION);
        return 0;
    }
    return Run(options);
}
