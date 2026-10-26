#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge {
namespace fs = std::filesystem;

struct SteamAchievement {
    std::string apiName;
    std::string displayName;
    std::string description;
};

struct SteamProject {
    bool enabled = false;
    std::uint32_t appId = 0;
    fs::path sdkPath; // SDK location on THIS developer's PC; never exported.
    std::vector<SteamAchievement> achievements;

    static SteamProject load(const fs::path& project);
    void save(const fs::path& project) const;
};
}
