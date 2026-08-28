#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kShotTableOffset = 0x268;
constexpr size_t kShotTableEntrySize = 8;
constexpr size_t kShotRecordSize = 0x34;

struct ShotSet {
    uint32_t offset{};
    uint32_t auxiliary{};
    size_t records{};
    std::set<uint32_t> callbackIndices;
};

struct ShtInfo {
    std::string file;
    size_t size{};
    uint16_t formatVersion{};
    uint16_t shotSetCount{};
    float fileHitbox{};
    float header08{};
    float header0C{};
    float normalSpeed{};
    float focusedSpeed{};
    float normalDiagonalSpeed{};
    float focusedDiagonalSpeed{};
    uint32_t maxPowerLevels{};
    uint32_t powerStep{};
    std::vector<ShotSet> shotSets;
};

template <typename T>
bool Read(const std::vector<uint8_t>& data, size_t offset, T& value) {
    if (offset > data.size() || sizeof(T) > data.size() - offset) {
        return false;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return true;
}

bool LoadFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), size);
    return input.good() || input.eof();
}

bool Parse(const std::string& path, ShtInfo& info, std::string& error) {
    std::vector<uint8_t> data;
    if (!LoadFile(path, data)) {
        error = "unable to read file";
        return false;
    }
    info.file = path;
    info.size = data.size();
    if (!Read(data, 0x00, info.formatVersion) ||
        !Read(data, 0x02, info.shotSetCount) ||
        !Read(data, 0x04, info.fileHitbox) ||
        !Read(data, 0x08, info.header08) ||
        !Read(data, 0x0C, info.header0C) ||
        !Read(data, 0x10, info.normalSpeed) ||
        !Read(data, 0x14, info.focusedSpeed) ||
        !Read(data, 0x18, info.normalDiagonalSpeed) ||
        !Read(data, 0x1C, info.focusedDiagonalSpeed) ||
        !Read(data, 0x20, info.maxPowerLevels) ||
        !Read(data, 0x24, info.powerStep)) {
        error = "truncated SHT header";
        return false;
    }
    if (info.shotSetCount > 64 ||
        kShotTableOffset + static_cast<size_t>(info.shotSetCount) * kShotTableEntrySize > data.size()) {
        error = "invalid shot-set table";
        return false;
    }

    for (uint16_t i = 0; i < info.shotSetCount; ++i) {
        ShotSet set;
        const size_t tableEntry = kShotTableOffset + static_cast<size_t>(i) * kShotTableEntrySize;
        Read(data, tableEntry, set.offset);
        Read(data, tableEntry + 4, set.auxiliary);
        if (set.offset >= data.size()) {
            error = "shot-set pointer outside file";
            return false;
        }

        size_t cursor = set.offset;
        bool foundSentinel = false;
        while (cursor < data.size()) {
            const int8_t cadence = static_cast<int8_t>(data[cursor]);
            if (cadence < 0) {
                foundSentinel = true;
                break;
            }
            if (cursor + kShotRecordSize > data.size()) {
                error = "truncated shot record";
                return false;
            }
            for (const size_t callbackOffset : {size_t{0x24}, size_t{0x28}, size_t{0x2C}, size_t{0x30}}) {
                uint32_t index = 0;
                Read(data, cursor + callbackOffset, index);
                set.callbackIndices.insert(index);
            }
            ++set.records;
            cursor += kShotRecordSize;
            if (set.records > 4096) {
                error = "missing shot-set sentinel";
                return false;
            }
        }
        if (!foundSentinel) {
            error = "shot-set sentinel outside file";
            return false;
        }
        info.shotSets.push_back(std::move(set));
    }
    return true;
}

std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool CheckKnownLayout(const ShtInfo& info, std::string& error) {
    if (info.formatVersion != 3 || info.shotSetCount != 10 ||
        info.maxPowerLevels != 4 || info.powerStep != 40) {
        error = "unexpected TH12 SHT header values";
        return false;
    }
    const std::string name = BaseName(info.file);
    float normal = 0.0f;
    float focused = 2.0f;
    if (name.rfind("pl00", 0) == 0 || name.rfind("pl02", 0) == 0) {
        normal = 4.5f;
    } else if (name.rfind("pl01", 0) == 0) {
        normal = 5.0f;
    } else {
        error = "unknown SHT filename";
        return false;
    }
    if (info.normalSpeed != normal || info.focusedSpeed != focused) {
        error = "unexpected movement parameters";
        return false;
    }
    return true;
}

std::string Json(const ShtInfo& info) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(7);
    out << "  {\n"
        << "    \"file\": \"" << BaseName(info.file) << "\",\n"
        << "    \"size\": " << info.size << ",\n"
        << "    \"format_version\": " << info.formatVersion << ",\n"
        << "    \"shot_set_count\": " << info.shotSetCount << ",\n"
        << "    \"header_raw\": {\"offset_04\": " << info.fileHitbox
        << ", \"offset_08\": " << info.header08
        << ", \"offset_0c\": " << info.header0C << "},\n"
        << "    \"movement\": {\"normal\": " << info.normalSpeed
        << ", \"focused\": " << info.focusedSpeed
        << ", \"normal_diagonal\": " << info.normalDiagonalSpeed
        << ", \"focused_diagonal\": " << info.focusedDiagonalSpeed << "},\n"
        << "    \"max_power_levels\": " << info.maxPowerLevels << ",\n"
        << "    \"power_step\": " << info.powerStep << ",\n"
        << "    \"shot_sets\": [\n";
    for (size_t i = 0; i < info.shotSets.size(); ++i) {
        const ShotSet& set = info.shotSets[i];
        out << "      {\"index\": " << i << ", \"offset\": " << set.offset
            << ", \"auxiliary\": " << set.auxiliary
            << ", \"records\": " << set.records << ", \"callback_indices\": [";
        size_t callback = 0;
        for (const uint32_t index : set.callbackIndices) {
            if (callback++ != 0) {
                out << ", ";
            }
            out << index;
        }
        out << "]}" << (i + 1 == info.shotSets.size() ? "\n" : ",\n");
    }
    out << "    ]\n  }";
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    bool check = false;
    std::string outputPath;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--check") {
            check = true;
        } else if (argument == "--json" && i + 1 < argc) {
            outputPath = argv[++i];
        } else {
            files.push_back(argument);
        }
    }
    if (files.empty()) {
        std::cerr << "usage: sht-inspect [--check] [--json output.json] file.sht...\n";
        return 2;
    }

    std::vector<ShtInfo> parsed;
    for (const std::string& file : files) {
        ShtInfo info;
        std::string error;
        if (!Parse(file, info, error) || (check && !CheckKnownLayout(info, error))) {
            std::cerr << file << ": " << error << "\n";
            return 1;
        }
        parsed.push_back(std::move(info));
    }

    std::ostringstream json;
    json << "{\n  \"schema\": 1,\n  \"airframes\": [\n";
    for (size_t i = 0; i < parsed.size(); ++i) {
        json << Json(parsed[i]) << (i + 1 == parsed.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";

    if (!outputPath.empty()) {
        std::ofstream output(outputPath, std::ios::binary);
        if (!output) {
            std::cerr << "unable to write " << outputPath << "\n";
            return 3;
        }
        output << json.str();
    } else {
        std::cout << json.str();
    }
    return 0;
}
