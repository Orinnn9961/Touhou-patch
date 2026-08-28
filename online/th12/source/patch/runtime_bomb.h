#pragma once

#include <cstdint>
#include <string>

namespace coop::th12 {

bool InitializeRuntimeBombs(const std::wstring& root,
                            std::wstring& error) noexcept;
bool CreateRuntimePlayer2Bomb() noexcept;
void DestroyRuntimePlayer2Bomb() noexcept;
void UpdateRuntimePlayer2Bomb(uint32_t inputMask, void* playerInf) noexcept;
void* RuntimePlayer2BombManager() noexcept;

}  // namespace coop::th12
