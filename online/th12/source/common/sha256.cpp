#include "sha256.h"

#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace coop {
namespace {

std::wstring Hex(const std::vector<unsigned char>& bytes) {
    static const wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

}  // namespace

bool Sha256File(const std::wstring& path, std::wstring& uppercaseHex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    DWORD hashLength = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    bool success = false;

    do {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                              &resultLength, 0) != 0 ||
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                              reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                              &resultLength, 0) != 0) {
            break;
        }
        object.resize(objectLength);
        digest.resize(hashLength);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) != 0) {
            break;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input) {
            break;
        }
        std::vector<unsigned char> buffer(1024 * 1024);
        bool hashingFailed = false;
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0 && BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) != 0) {
                hashingFailed = true;
                break;
            }
        }
        if (hashingFailed || input.bad() || BCryptFinishHash(hash, digest.data(), hashLength, 0) != 0) {
            break;
        }
        uppercaseHex = Hex(digest);
        success = true;
    } while (false);

    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return success;
}

}  // namespace coop
