#include "log.h"
#include "path_util.h"

#include <windows.h>
#include <deque>
#include <fstream>
#include <map>
#include <utility>

namespace coop {
namespace {

struct PendingLine {
    std::wstring path;
    std::string line;
};

struct AsyncLogState {
    CRITICAL_SECTION lock{};
    HANDLE wake{};
    std::deque<PendingLine> queue;
    LONG dropped{};
};

INIT_ONCE g_logInit = INIT_ONCE_STATIC_INIT;
AsyncLogState* g_asyncState = nullptr;
volatile LONG g_asyncEnabled = 0;

BOOL CALLBACK InitializeAsyncLogState(PINIT_ONCE, PVOID, PVOID*) {
    auto* state = new (std::nothrow) AsyncLogState();
    if (state == nullptr) {
        return FALSE;
    }
    InitializeCriticalSection(&state->lock);
    state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (state->wake == nullptr) {
        DeleteCriticalSection(&state->lock);
        delete state;
        return FALSE;
    }
    g_asyncState = state;
    return TRUE;
}

void WritePendingBatch(const std::wstring& path,
                       const std::string& lines) noexcept {
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        std::wstring directory = path.substr(0, separator);
        EnsureDirectory(directory);
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(file, lines.data(), static_cast<DWORD>(lines.size()),
              &written, nullptr);
    CloseHandle(file);
}

DWORD WINAPI AsyncLogThread(void* argument) {
    auto* state = static_cast<AsyncLogState*>(argument);
    for (;;) {
        WaitForSingleObject(state->wake, 250);
        std::deque<PendingLine> pending;
        EnterCriticalSection(&state->lock);
        pending.swap(state->queue);
        LeaveCriticalSection(&state->lock);
        std::map<std::wstring, std::string> batches;
        for (const auto& line : pending) {
            batches[line.path].append(line.line);
        }
        for (const auto& batch : batches) {
            WritePendingBatch(batch.first, batch.second);
        }
    }
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}

std::wstring Timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[64]{};
    wsprintfW(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
              time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
              time.wSecond, time.wMilliseconds);
    return buffer;
}

}  // namespace

void EnableAsyncLogging() noexcept {
    if (InterlockedCompareExchange(&g_asyncEnabled, 1, 0) != 0) {
        return;
    }
    if (!InitOnceExecuteOnce(&g_logInit, InitializeAsyncLogState, nullptr,
                             nullptr) || g_asyncState == nullptr) {
        InterlockedExchange(&g_asyncEnabled, 0);
        return;
    }
    HANDLE thread = CreateThread(nullptr, 0, AsyncLogThread, g_asyncState, 0,
                                 nullptr);
    if (thread == nullptr) {
        InterlockedExchange(&g_asyncEnabled, 0);
        return;
    }
    SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(thread);
}

void WriteLog(const std::wstring& root, const wchar_t* fileName, const std::wstring& message) {
    const std::wstring logDirectory = JoinPath(root, L"coop\\logs");
    const std::wstring path = JoinPath(logDirectory, fileName);
    const std::string line = Utf8(L"[" + Timestamp() + L"] " + message + L"\r\n");
    if (InterlockedCompareExchange(&g_asyncEnabled, 0, 0) != 0 &&
        g_asyncState != nullptr) {
        EnterCriticalSection(&g_asyncState->lock);
        constexpr size_t kMaxPendingLines = 4096;
        if (g_asyncState->queue.size() >= kMaxPendingLines) {
            g_asyncState->queue.pop_front();
            InterlockedIncrement(&g_asyncState->dropped);
        }
        g_asyncState->queue.push_back({path, line});
        LeaveCriticalSection(&g_asyncState->lock);
        SetEvent(g_asyncState->wake);
        return;
    }
    EnsureDirectory(JoinPath(root, L"coop"));
    EnsureDirectory(logDirectory);
    std::ofstream output(path, std::ios::app | std::ios::binary);
    if (output) {
        output.write(line.data(), static_cast<std::streamsize>(line.size()));
    }
}

}  // namespace coop
