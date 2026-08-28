#pragma once

#include <windows.h>
#include <string>

namespace coop {

std::wstring ModuleDirectory(HMODULE module);
std::wstring JoinPath(const std::wstring& left, const wchar_t* right);
bool EnsureDirectory(const std::wstring& path);

}  // namespace coop
