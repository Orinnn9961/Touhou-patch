#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace coop::th12 {

class RelativeCallPatch {
public:
    RelativeCallPatch() = default;
    RelativeCallPatch(uintptr_t callSite, uintptr_t expectedTarget) noexcept;

    bool Install(void* replacement) noexcept;
    bool Restore() noexcept;
    bool IsInstalled() const noexcept;

private:
    uintptr_t callSite_{};
    uintptr_t expectedTarget_{};
    std::array<uint8_t, 5> original_{};
    bool captured_{};
    bool installed_{};
};

class CodePatch {
public:
    CodePatch() = default;
    CodePatch(uintptr_t address, std::vector<uint8_t> expected,
              std::vector<uint8_t> replacement) noexcept;

    bool Install() noexcept;
    bool Restore() noexcept;
    bool IsInstalled() const noexcept;

private:
    uintptr_t address_{};
    std::vector<uint8_t> expected_{};
    std::vector<uint8_t> replacement_{};
    std::vector<uint8_t> original_{};
    bool captured_{};
    bool installed_{};
};

}  // namespace coop::th12
