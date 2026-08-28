#pragma once

#include <string>

namespace coop {

void EnableAsyncLogging() noexcept;
void WriteLog(const std::wstring& root, const wchar_t* fileName, const std::wstring& message);

}  // namespace coop
