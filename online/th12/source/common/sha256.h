#pragma once

#include <string>

namespace coop {

bool Sha256File(const std::wstring& path, std::wstring& uppercaseHex);

}  // namespace coop
