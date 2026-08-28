#pragma once

#include "player_context.h"

#include <string>

namespace coop::th12 {

bool InitializeRuntimePlayerContexts(std::wstring& error) noexcept;
PlayerContextManager* RuntimePlayerContexts() noexcept;

}  // namespace coop::th12
