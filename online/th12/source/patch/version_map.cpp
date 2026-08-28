#include "version_map.h"

#include <cstring>

namespace coop::th12 {
namespace {

constexpr uintptr_t OptionLayout(size_t combinedIndex) {
    return GameAddresses::kOptionLayoutTable + combinedIndex * 0x40;
}

bool HasBytes(uintptr_t address, const unsigned char* expected, size_t count) {
    return std::memcmp(reinterpret_cast<const void*>(address), expected, count) == 0;
}

bool HasString(uintptr_t address, const char* expected) {
    return std::strcmp(reinterpret_cast<const char*>(address), expected) == 0;
}

}  // namespace

const std::array<AirframeDescriptor, 6> kAirframes{{
    {"reimu_a", "Reimu", 0, 0, "pl00.anm", "pl00a.sht", 21,
     4.5f, 2.0f, 3.1819805f, 1.4142135f, 2.0f,
     OptionLayout(0), 0x0046A5F0, 0x0046A970},
    {"reimu_b", "Reimu", 0, 1, "pl00.anm", "pl00b.sht", 22,
     4.5f, 2.0f, 3.1819805f, 1.4142135f, 2.0f,
     OptionLayout(1), 0x00408120, 0x004082E0},
    {"marisa_a", "Marisa", 1, 0, "pl01.anm", "pl01a.sht", 13,
     5.0f, 2.0f, 3.5355339f, 1.4142135f, 3.5f,
     OptionLayout(2), 0x00407010, 0x004072D0},
    {"marisa_b", "Marisa", 1, 1, "pl01.anm", "pl01b.sht", 14,
     5.0f, 2.0f, 3.5355339f, 1.4142135f, 3.5f,
     OptionLayout(3), 0x00407780, 0x00407950},
    {"sanae_a", "Sanae", 2, 0, "pl02.anm", "pl02a.sht", 17,
     4.5f, 2.0f, 3.1819805f, 1.4142135f, 3.0f,
     OptionLayout(4), 0x004089C0, 0x00408B90},
    {"sanae_b", "Sanae", 2, 1, "pl02.anm", "pl02b.sht", 18,
     4.5f, 2.0f, 3.1819805f, 1.4142135f, 3.0f,
     OptionLayout(5), 0x00408CB0, 0x00408F00},
}};

bool IsTh12Process() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    const wchar_t* name = path;
    for (const wchar_t* current = path; *current != L'\0'; ++current) {
        if (*current == L'\\' || *current == L'/') {
            name = current + 1;
        }
    }
    return _wcsicmp(name, L"th12.exe") == 0;
}

bool ValidateAddressMap(std::wstring& error) {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != GameAddresses::kImageBase) {
        error = L"unexpected executable image base";
        return false;
    }

    static constexpr unsigned char playerInit[] = {0xA1, 0x90, 0x0C, 0x4B, 0x00};
    static constexpr unsigned char shtLoad[] = {0x6A, 0x00, 0x6A, 0x00, 0xE8};
    static constexpr unsigned char shotDispatch[] = {0x51, 0x53, 0x8B, 0xD8, 0xA1, 0x48, 0x0C, 0x4B, 0x00};
    static constexpr unsigned char shotUpdate[] = {0x51, 0x55, 0x8B, 0x6C, 0x24, 0x0C};
    static constexpr unsigned char playerBulletUpdate[] = {0x83, 0xEC, 0x40, 0x8B, 0x54, 0x24, 0x44};
    static constexpr unsigned char optionRebuild[] = {0x83, 0xEC, 0x38, 0x8B, 0x0D, 0x90, 0x0C, 0x4B, 0x00};
    static constexpr unsigned char bombStart[] = {0x57, 0x8B, 0x3D, 0xC4, 0x43, 0x4B, 0x00};
    static constexpr unsigned char bombUpdate[] = {
        0x53, 0x56, 0x8B, 0xF0, 0x8B, 0x46, 0x3C};
    static constexpr unsigned char bombCreate[] = {
        0x56, 0x68, 0x24, 0x05, 0x00, 0x00, 0xE8};
    static constexpr unsigned char bombDestroy[] = {
        0x6A, 0xFF, 0x68, 0xDB, 0x75, 0x49, 0x00};
    static constexpr unsigned char bombConsume[] = {
        0xA1, 0xA0, 0x0C, 0x4B, 0x00, 0x83, 0xE8, 0x01};
    static constexpr unsigned char damageAreaAngleNormalize[] = {
        0xD9, 0x44, 0x24, 0x04, 0x33, 0xC9, 0xDD, 0x05};
    static constexpr unsigned char damageAreaPolarUpdate[] = {
        0x51, 0x89, 0x0C, 0x24, 0x8B, 0x04, 0x24, 0xD9};
    static constexpr unsigned char damageAreaVectorUpdate[] = {
        0x8B, 0x43, 0x30, 0x83, 0xEC, 0x1C, 0x56, 0x57};
    static constexpr unsigned char playerDestroy[] = {0x6A, 0xFF, 0x68, 0xB9, 0x70, 0x49, 0x00};
    static constexpr unsigned char playerMovement[] = {0xA1, 0xD0, 0x49, 0x4D, 0x00, 0x83, 0xEC, 0x20};
    static constexpr unsigned char playerUpdateCallback[] = {0x51, 0xE8, 0x3A, 0xF5, 0xFF, 0xFF, 0xC3};
    static constexpr unsigned char playerRenderCallback[] = {0x8B, 0xC1, 0x51, 0xE8, 0x88, 0xFF, 0xFF, 0xFF, 0x59, 0xC3};
    static constexpr unsigned char replayInputCallback[] = {
        0x8B, 0xC1, 0xE9, 0xC9, 0xF2, 0xFF, 0xFF};
    static constexpr unsigned char crtThreadData[] = {0x8B, 0xFF, 0x56, 0xE8};
    static constexpr unsigned char gameRngInitialization[] = {
        0xFF, 0x15, 0x98, 0x82, 0x49, 0x00,
        0xA3, 0x7C, 0xEE, 0x4C, 0x00,
        0x66, 0xA3, 0x68, 0xE5, 0x4C, 0x00,
        0x66, 0xA3, 0x60, 0xE5, 0x4C, 0x00};
    static constexpr unsigned char anmSetScript[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
    static constexpr unsigned char enemyBulletScreenClear[] = {
        0x51, 0xA1, 0xCC, 0x43, 0x4B, 0x00, 0x56};
    static constexpr unsigned char enemyLaserScreenClear[] = {
        0xA1, 0xF4, 0x44, 0x4B, 0x00, 0x8B, 0x48, 0x18};
    static constexpr unsigned char enemySetLife[] = {
        0x33, 0xC9, 0x8B, 0xC3, 0xE8, 0xAB, 0x2E, 0x00, 0x00,
        0xF7, 0x83, 0xB8, 0x16, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
        0x89, 0x83, 0x08, 0x16, 0x00, 0x00,
        0x89, 0x83, 0x0C, 0x16, 0x00, 0x00};
    static constexpr unsigned char enemyDamageApply[] = {
        0x8D, 0x8B, 0x08, 0x16, 0x00, 0x00,
        0x8B, 0xC6,
        0xE8, 0xB1, 0xE1, 0xFF, 0xFF,
        0x8B, 0x8B, 0x4C, 0x17, 0x00, 0x00};
    static constexpr unsigned char enemySpellStart[] = {
        0x8B, 0xCE, 0xE8, 0x19, 0x57, 0xFF, 0xFF,
        0x83, 0x8B, 0x1C, 0x16, 0x00, 0x00, 0x01,
        0x8B, 0x83, 0x08, 0x16, 0x00, 0x00};
    static constexpr unsigned char enemyMessageUpdateCall[] = {
        0xE8, 0x85, 0x15, 0x00, 0x00};
    static constexpr unsigned char callbackMessageUpdateCall[] = {
        0xE8, 0x17, 0x00, 0x00, 0x00};
    static constexpr unsigned char messageUpdate[] = {
        0x83, 0xEC, 0x48, 0x53, 0x55};

    if (!HasBytes(GameAddresses::kPlayerInitialize, playerInit, sizeof(playerInit)) ||
        !HasBytes(GameAddresses::kShtLoadAndRelocate, shtLoad, sizeof(shtLoad)) ||
        !HasBytes(GameAddresses::kShotDispatch, shotDispatch, sizeof(shotDispatch)) ||
        !HasBytes(GameAddresses::kShotUpdate, shotUpdate, sizeof(shotUpdate)) ||
        !HasBytes(GameAddresses::kPlayerBulletUpdate, playerBulletUpdate,
                  sizeof(playerBulletUpdate)) ||
        !HasBytes(GameAddresses::kOptionRebuild, optionRebuild,
                  sizeof(optionRebuild)) ||
        !HasBytes(GameAddresses::kBombStartDispatch, bombStart, sizeof(bombStart)) ||
        !HasBytes(GameAddresses::kBombUpdateDispatch, bombUpdate,
                  sizeof(bombUpdate)) ||
        !HasBytes(GameAddresses::kBombManagerCreate, bombCreate,
                  sizeof(bombCreate)) ||
        !HasBytes(GameAddresses::kBombManagerDestroy, bombDestroy,
                  sizeof(bombDestroy)) ||
        !HasBytes(GameAddresses::kBombConsume, bombConsume,
                  sizeof(bombConsume)) ||
        !HasBytes(GameAddresses::kDamageAreaAngleNormalize,
                  damageAreaAngleNormalize, sizeof(damageAreaAngleNormalize)) ||
        !HasBytes(GameAddresses::kDamageAreaPolarUpdate,
                  damageAreaPolarUpdate, sizeof(damageAreaPolarUpdate)) ||
        !HasBytes(GameAddresses::kDamageAreaVectorUpdate,
                  damageAreaVectorUpdate, sizeof(damageAreaVectorUpdate)) ||
        !HasBytes(GameAddresses::kPlayerDestroy, playerDestroy, sizeof(playerDestroy)) ||
        !HasBytes(GameAddresses::kPlayerMovement, playerMovement, sizeof(playerMovement)) ||
        !HasBytes(GameAddresses::kPlayerUpdateCallback, playerUpdateCallback,
                  sizeof(playerUpdateCallback)) ||
        !HasBytes(GameAddresses::kPlayerRenderCallback, playerRenderCallback,
                  sizeof(playerRenderCallback)) ||
        !HasBytes(GameAddresses::kReplayInputCallback, replayInputCallback,
                  sizeof(replayInputCallback)) ||
        !HasBytes(GameAddresses::kCrtThreadData, crtThreadData,
                  sizeof(crtThreadData)) ||
        !HasBytes(GameAddresses::kGameRngInitialization,
                  gameRngInitialization, sizeof(gameRngInitialization)) ||
        !HasBytes(GameAddresses::kAnmSetScript, anmSetScript,
                  sizeof(anmSetScript)) ||
        !HasBytes(GameAddresses::kEnemyBulletScreenClear,
                  enemyBulletScreenClear, sizeof(enemyBulletScreenClear)) ||
        !HasBytes(GameAddresses::kEnemyLaserScreenClear,
                  enemyLaserScreenClear, sizeof(enemyLaserScreenClear)) ||
        !HasBytes(GameAddresses::kEnemySetLifeReadCall - 4,
                  enemySetLife, sizeof(enemySetLife)) ||
        !HasBytes(GameAddresses::kEnemyDamageApplyCall - 8,
                  enemyDamageApply, sizeof(enemyDamageApply)) ||
        !HasBytes(GameAddresses::kEnemySpellStartCall - 2,
                  enemySpellStart, sizeof(enemySpellStart)) ||
        !HasBytes(GameAddresses::kEnemyMessageUpdateCall,
                  enemyMessageUpdateCall, sizeof(enemyMessageUpdateCall)) ||
        !HasBytes(GameAddresses::kCallbackMessageUpdateCall,
                  callbackMessageUpdateCall,
                  sizeof(callbackMessageUpdateCall)) ||
        !HasBytes(GameAddresses::kMessageUpdate,
                  messageUpdate, sizeof(messageUpdate))) {
        error = L"one or more phase 2 code signatures do not match th12 1.00b";
        return false;
    }

    static constexpr uintptr_t anmStrings[] = {0x004A10B4, 0x004A10A8, 0x004A109C};
    static constexpr const char* anmNames[] = {"pl00.anm", "pl01.anm", "pl02.anm"};
    static constexpr uintptr_t shtStrings[] = {
        0x004A1090, 0x004A1084, 0x004A1078, 0x004A106C, 0x004A1060, 0x004A1054};
    for (size_t i = 0; i < 3; ++i) {
        if (!HasString(anmStrings[i], anmNames[i])) {
            error = L"ANM resource table does not match";
            return false;
        }
    }
    for (size_t i = 0; i < kAirframes.size(); ++i) {
        if (!HasString(shtStrings[i], kAirframes[i].shtFile)) {
            error = L"SHT resource table does not match";
            return false;
        }
    }
    return true;
}

}  // namespace coop::th12
