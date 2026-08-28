#include "path_util.h"

namespace coop {

std::wstring ModuleDirectory(HMODULE module) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }
    std::wstring path(buffer, length);
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring JoinPath(const std::wstring& left, const wchar_t* right) {
    if (left.empty()) {
        return right;
    }
    if (left.back() == L'\\' || left.back() == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

bool EnsureDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr) != 0) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace coop
