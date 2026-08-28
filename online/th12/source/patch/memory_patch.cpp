#include "memory_patch.h"

#include <windows.h>

#include <climits>
#include <cstring>
#include <utility>

namespace coop::th12 {

RelativeCallPatch::RelativeCallPatch(uintptr_t callSite, uintptr_t expectedTarget) noexcept
    : callSite_(callSite), expectedTarget_(expectedTarget) {}

bool RelativeCallPatch::Install(void* replacement) noexcept {
    if (callSite_ == 0 || expectedTarget_ == 0 || replacement == nullptr || installed_) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(callSite_);
    if (bytes[0] != 0xE8) {
        return false;
    }
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes + 1, sizeof(displacement));
    const uintptr_t currentTarget = callSite_ + 5 + static_cast<intptr_t>(displacement);
    if (currentTarget != expectedTarget_) {
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(callSite_), original_.size(),
                        PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(original_.data(), bytes, original_.size());
    const intptr_t replacementDisplacement =
        reinterpret_cast<uintptr_t>(replacement) - (callSite_ + 5);
    if (replacementDisplacement < INT32_MIN || replacementDisplacement > INT32_MAX) {
        DWORD ignored = 0;
        VirtualProtect(reinterpret_cast<void*>(callSite_), original_.size(), oldProtection,
                       &ignored);
        return false;
    }
    const uint8_t opcode = 0xE8;
    std::memcpy(reinterpret_cast<void*>(callSite_), &opcode, sizeof(opcode));
    const int32_t newDisplacement = static_cast<int32_t>(replacementDisplacement);
    std::memcpy(reinterpret_cast<void*>(callSite_ + 1), &newDisplacement,
                sizeof(newDisplacement));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(callSite_),
                          original_.size());
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(callSite_), original_.size(), oldProtection, &ignored);
    captured_ = true;
    installed_ = true;
    return true;
}

bool RelativeCallPatch::Restore() noexcept {
    if (!captured_ || !installed_) {
        return !installed_;
    }
    DWORD oldProtection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(callSite_), original_.size(),
                        PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(callSite_), original_.data(), original_.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(callSite_),
                          original_.size());
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(callSite_), original_.size(), oldProtection, &ignored);
    installed_ = false;
    return true;
}

bool RelativeCallPatch::IsInstalled() const noexcept {
    return installed_;
}

CodePatch::CodePatch(uintptr_t address, std::vector<uint8_t> expected,
                     std::vector<uint8_t> replacement) noexcept
    : address_(address), expected_(std::move(expected)),
      replacement_(std::move(replacement)) {}

bool CodePatch::Install() noexcept {
    if (address_ == 0 || installed_ || expected_.empty() ||
        expected_.size() != replacement_.size() ||
        std::memcmp(reinterpret_cast<const void*>(address_), expected_.data(),
                    expected_.size()) != 0) {
        return false;
    }
    DWORD oldProtection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address_), replacement_.size(),
                        PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    original_.assign(reinterpret_cast<const uint8_t*>(address_),
                     reinterpret_cast<const uint8_t*>(address_) + expected_.size());
    std::memcpy(reinterpret_cast<void*>(address_), replacement_.data(),
                replacement_.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address_),
                          replacement_.size());
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(address_), replacement_.size(), oldProtection,
                   &ignored);
    captured_ = true;
    installed_ = true;
    return true;
}

bool CodePatch::Restore() noexcept {
    if (!captured_ || !installed_) {
        return !installed_;
    }
    DWORD oldProtection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address_), original_.size(),
                        PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address_), original_.data(), original_.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address_),
                          original_.size());
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(address_), original_.size(), oldProtection,
                   &ignored);
    installed_ = false;
    return true;
}

bool CodePatch::IsInstalled() const noexcept {
    return installed_;
}

}  // namespace coop::th12
