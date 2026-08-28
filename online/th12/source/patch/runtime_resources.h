#pragma once

#include "resource_bank.h"

#include <string>

namespace coop::th12 {

bool InitializeRuntimeResources(const std::wstring& root, std::wstring& error) noexcept;
DualResourceBanks* RuntimeResourceBanks() noexcept;
bool BindPlayer2Resources(void* playerInf) noexcept;
void ReleaseRuntimePlayer2Resources() noexcept;

}  // namespace coop::th12
