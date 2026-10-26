#include "SteamProject.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <cctype>

namespace forge {
using nlohmann::json;

SteamProject SteamProject::load(const fs::path& project) {
    SteamProject result;
    const fs::path file = project / "steam.json";
    if (!fs::exists(file)) return result;
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Could not read " + file.string());
    const json j = json::parse(input);
    result.enabled = j.value("enabled", false);
    result.appId = j.value("appId", 0u);
    result.sdkPath = j.value("sdkPath", std::string{});
    for (const auto& a : j.value("achievements", json::array())) {
        result.achievements.push_back({a.at("apiName").get<std::string>(),
            a.value("displayName", std::string{}), a.value("description", std::string{})});
    }
    return result;
}

void SteamProject::save(const fs::path& project) const {
    json j = {{"enabled", enabled}, {"appId", appId},
              {"sdkPath", sdkPath.string()}, {"achievements", json::array()}};
    for (const auto& a : achievements) {
        if (a.apiName.empty() || a.apiName.size() > 127) throw std::runtime_error("Invalid achievement API name length");
        for (unsigned char c : a.apiName) {
            if (!(std::isalnum(c) || c == '_')) throw std::runtime_error("Achievement API names can contain letters, digits and underscores only");
        }
        for (const auto& b : achievements) {
            if (&a != &b && a.apiName == b.apiName) throw std::runtime_error("Duplicate Steam achievement ID: " + a.apiName);
        }
        j["achievements"].push_back({{"apiName", a.apiName},
            {"displayName", a.displayName}, {"description", a.description}});
    }
    if (enabled && appId == 0) throw std::runtime_error("Set your Steam App ID before enabling Steamworks");
    fs::create_directories(project);
    const fs::path path = project / "steam.json";
    std::ofstream out(path, std::ios::trunc);
    if (!out || !(out << j.dump(2) << '\n')) throw std::runtime_error("Unable to save steam.json");
}
} // namespace forge
