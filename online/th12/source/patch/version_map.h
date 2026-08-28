#pragma once

#include <windows.h>
#include <array>
#include <cstdint>
#include <string>

namespace coop::th12 {

struct GameAddresses {
    static constexpr uintptr_t kImageBase = 0x00400000;
    static constexpr uintptr_t kCharacter = 0x004B0C90;
    static constexpr uintptr_t kShotType = 0x004B0C94;
    static constexpr uintptr_t kPlayerSingleton = 0x004B4514;
    static constexpr uintptr_t kInputMask = 0x004D49D0;
    static constexpr uintptr_t kInputPreviousMask = 0x004D49D4;
    static constexpr uintptr_t kRawInputMask = 0x004D48B8;
    static constexpr uintptr_t kRawInputPreviousMask = 0x004D48BC;
    static constexpr uintptr_t kPower = 0x004B0C48;
    static constexpr uintptr_t kScore = 0x004B0C44;
    static constexpr uintptr_t kLives = 0x004B0C98;
    static constexpr uintptr_t kLifeFragments = 0x004B0C9C;
    static constexpr uintptr_t kBombs = 0x004B0CA0;
    static constexpr uintptr_t kBombFragments = 0x004B0CA4;
    static constexpr uintptr_t kPointValue = 0x004B0CCC;
    static constexpr uintptr_t kUfo0 = 0x004B0C4C;
    static constexpr uintptr_t kUfo1 = 0x004B0C50;
    static constexpr uintptr_t kUfo2 = 0x004B0C54;
    static constexpr uintptr_t kUfoState = 0x004B0C58;
    static constexpr uintptr_t kUfoFlags = 0x004B0C5C;
    static constexpr uintptr_t kDeathPowerLoss = 0x004B0CD4;
    static constexpr uintptr_t kGameTimeScale = 0x004B2ED0;
    static constexpr uintptr_t kGameManager = 0x004B43DC;
    static constexpr uintptr_t kLifeUiManager = 0x004B43E4;
    static constexpr uintptr_t kLifeUiUpdate = 0x0041CE60;
    static constexpr uintptr_t kBombUiUpdate = 0x0041CF40;

    static constexpr uintptr_t kPlayerConstructor = 0x004359A0;
    static constexpr uintptr_t kPlayerInitialize = 0x00435AE0;
    static constexpr uintptr_t kPlayerMovement = 0x004364F0;
    static constexpr uintptr_t kPlayerCreate = 0x00436410;
    static constexpr uintptr_t kPlayerDestroy = 0x00436270;
    static constexpr uintptr_t kPlayerDelete = 0x004364B0;
    static constexpr uintptr_t kPlayerDeleteSingleton = 0x004364D0;
    static constexpr uintptr_t kPlayerUpdate = 0x00436BA0;
    static constexpr uint32_t kPlayerInfSize = 0xC59C;
    static constexpr uint32_t kPlayerAnmPointerOffset = 0x10;
    static constexpr uint32_t kPlayerShtPointerOffset = 0xA2C;
    static constexpr uintptr_t kPlayerInitializeCallSite = 0x00436465;
    static constexpr uintptr_t kAnmManagerRoot = 0x004CE8CC;
    static constexpr uintptr_t kAnmSlotTableOffset = 0x004B50C0;
    static constexpr uintptr_t kAnmDestroyArchive = 0x004604E0;
    static constexpr uintptr_t kAnmDeleteVm = 0x00461970;
    static constexpr uintptr_t kAnmDeleteVmImmediate = 0x004619E0;
    static constexpr uintptr_t kAnmFindVm = 0x00461920;
    static constexpr uintptr_t kAnmSetScript = 0x00454D10;
    static constexpr uintptr_t kGameAllocate = 0x0046C9EA;
    static constexpr uintptr_t kGameFree = 0x0046CA4F;
    static constexpr uintptr_t kCrtThreadData = 0x00475467;
    static constexpr uintptr_t kGameRngInitialization = 0x0042F582;
    static constexpr uintptr_t kGameRngSecondary = 0x004CE560;
    static constexpr uintptr_t kGameRngPrimary = 0x004CE568;
    static constexpr uintptr_t kPreloadedPlayerSht = 0x004CE8A8;
    static constexpr uintptr_t kPlayerPreserveResourcesFlags = 0x004B0CE0;
    static constexpr uintptr_t kPlayerUpdateCallback = 0x00437660;
    static constexpr uintptr_t kPlayerRenderCallback = 0x00437670;
    static constexpr uintptr_t kReplayInputUpdate = 0x0043B7E0;
    static constexpr uintptr_t kReplayInputCallback = 0x0043C510;
    static constexpr uintptr_t kReplayInputCallbackJump = 0x0043C512;
    static constexpr uintptr_t kMessageUpdate = 0x0041FCC0;
    static constexpr uintptr_t kEnemyMessageUpdateCall = 0x0041E736;
    static constexpr uintptr_t kCallbackMessageUpdateCall = 0x0041FCA4;
    static constexpr uintptr_t kUpdateScheduler = 0x004624C0;
    static constexpr std::array<uintptr_t, 3> kGameplayUpdateCallSites{
        0x00450103, 0x004504B1, 0x00450650,
    };

    static constexpr uintptr_t kAnmLoad = 0x0045FE60;
    static constexpr uintptr_t kAnmUnloadSlot = 0x004604A0;
    static constexpr uintptr_t kAnmCleanup = 0x004603D0;
    static constexpr uint32_t kPlayerAnmSlot = 7;

    static constexpr uintptr_t kShtLoadAndRelocate = 0x00437680;
    static constexpr uintptr_t kShotSpawn = 0x00439630;
    static constexpr uintptr_t kShotDispatch = 0x004399D0;
    static constexpr uintptr_t kShotUpdate = 0x00439A40;
    static constexpr uintptr_t kPlayerBulletUpdate = 0x00439B10;
    static constexpr uintptr_t kOptionRebuild = 0x004385B0;
    static constexpr uintptr_t kPlayerDamageScan = 0x00439ED0;
    static constexpr uintptr_t kPlayerDamageScanCall1 = 0x00413D8B;
    static constexpr uintptr_t kPlayerDamageScanCall2 = 0x0041AC77;
    static constexpr uintptr_t kEnemyReadIntParameter = 0x0041A900;
    static constexpr uintptr_t kEnemySetLifeReadCall = 0x00417A50;
    static constexpr uintptr_t kEnemyDamageApply = 0x00412050;
    static constexpr uintptr_t kEnemyDamageApplyCall = 0x00413E9A;
    static constexpr uintptr_t kEnemySpellStart = 0x0040E060;
    static constexpr uintptr_t kEnemySpellStartCall = 0x00418942;
    static constexpr uintptr_t kPlayerDeathAreaSpawn = 0x004390F0;
    static constexpr uintptr_t kEnemyBulletScreenClear = 0x0040D230;
    static constexpr uintptr_t kEnemyLaserScreenClear = 0x00428750;
    static constexpr uintptr_t kPlayerLosePower = 0x00439440;
    static constexpr uintptr_t kPlayerGrazeSettlement = 0x004391C0;
    static constexpr uintptr_t kPlayerAngleToPosition = 0x00437730;
    static constexpr uintptr_t kItemSpawn = 0x004273F0;

    static constexpr uintptr_t kPlayerRectCollision = 0x00437810;
    static constexpr uintptr_t kPlayerCircleCollision = 0x00437980;
    static constexpr uintptr_t kPlayerRotatedCollision = 0x00437A80;

    static constexpr uintptr_t kItemLoopStateLoad = 0x00425C47;
    static constexpr uintptr_t kItemLoopAdvance = 0x00426F53;
    static constexpr uintptr_t kAimPlayerLoad = 0x004377A9;

    static constexpr uintptr_t kBombStartDispatch = 0x00406BF0;
    static constexpr uintptr_t kBombUpdateDispatch = 0x00406CE0;
    static constexpr uintptr_t kBombManagerCreate = 0x00406B20;
    static constexpr uintptr_t kBombManagerDestroy = 0x00406930;
    static constexpr uintptr_t kBombConsume = 0x00422F20;
    static constexpr uintptr_t kBombManager = 0x004B43C4;
    // Player damage-area motion helpers used by Bomb-generated attack regions.
    static constexpr uintptr_t kDamageAreaAngleNormalize = 0x004646E0;
    static constexpr uintptr_t kDamageAreaPolarUpdate = 0x00465390;
    static constexpr uintptr_t kDamageAreaVectorUpdate = 0x00464DB0;

    static constexpr uintptr_t kAnmNameTable = 0x004B3184;
    static constexpr uintptr_t kShtNameTable = 0x004B3190;
    static constexpr uintptr_t kOptionLayoutTable = 0x004B31D8;
};

struct AirframeDescriptor {
    const char* id;
    const char* characterName;
    uint8_t character;
    uint8_t shotType;
    const char* anmFile;
    const char* shtFile;
    uint8_t optionAnmScript;
    float normalSpeed;
    float focusedSpeed;
    float normalDiagonalSpeed;
    float focusedDiagonalSpeed;
    float effectiveHitboxRadius;
    uintptr_t optionLayout;
    uintptr_t bombStart;
    uintptr_t bombUpdate;
};

extern const std::array<AirframeDescriptor, 6> kAirframes;

bool IsTh12Process();
bool ValidateAddressMap(std::wstring& error);

}  // namespace coop::th12
