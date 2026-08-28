#pragma once

#include <cstdint>
#include <string>

namespace coop::th12 {

bool InitializeRuntimePlayer2(const std::wstring& root, std::wstring& error) noexcept;
bool CreateRuntimePlayer2() noexcept;
void DestroyRuntimePlayer2() noexcept;
void* RuntimePlayer2() noexcept;
bool RuntimePlayer1Eliminated() noexcept;
uint32_t RuntimePlayer2RecoveryState() noexcept;
bool RuntimePlayer2DeathbombAvailable() noexcept;
bool PrepareRuntimePlayer2Deathbomb() noexcept;
void CancelRuntimePlayer2RecoveryForDeathbomb() noexcept;
void RollbackRuntimePlayer2Deathbomb() noexcept;
int OnPlayer2UpdateTick(void* playerInf) noexcept;
int OnPlayer1UpdateTick(void* playerInf) noexcept;
int OnPlayerRenderTick(void* playerInf) noexcept;
int32_t ConsumeRuntimeLifeGainForRevival(int32_t gain) noexcept;

}  // namespace coop::th12
