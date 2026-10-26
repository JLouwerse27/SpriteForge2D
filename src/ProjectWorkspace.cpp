#include "ProjectWorkspace.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace forge {
using json = nlohmann::json;
namespace {
const char* projectCmake = R"CMAKE(cmake_minimum_required(VERSION 3.28)
project(SpriteForgeGame LANGUAGES CXX)
if(NOT DEFINED SPRITEFORGE_SDK OR NOT EXISTS "${SPRITEFORGE_SDK}/cmake/SpriteForgeGame.cmake")
    message(FATAL_ERROR "Build through SpriteForge or pass -DSPRITEFORGE_SDK=<path-to-editor-sdk>")
endif()
include("${SPRITEFORGE_SDK}/cmake/SpriteForgeGame.cmake")
)CMAKE";

void writeText(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !(file << contents)) throw std::runtime_error("Cannot write " + path.string());
}

std::string escapeCpp(const std::string& value) {
    // JSON quoting also makes valid C++ double-quoted UTF-8 string literals for ordinary text.
    return json(value).dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string floatLiteral(float value) {
    if (!std::isfinite(value)) throw std::runtime_error("Non-finite behavior setting");
    std::ostringstream out;
    out << std::setprecision(9) << value;
    std::string result = out.str();
    if (result.find_first_of(".eE") == std::string::npos) result += ".0";
    return result + "f";
}

bool classIdentifier(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '_')) return false;
    return true;
}

bool validCodeFile(const std::string& s) {
    if (s.empty() || s == "." || s == ".." || s[0] == '.' || s.find("..") != std::string::npos ||
        s.find_first_of("/\\:\"<>|?*") != std::string::npos) return false;
    const std::string ext = fs::path(s).extension().string();
    return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".inl";
}
}

ProjectWorkspace::ProjectWorkspace(fs::path root) : root_(std::move(root)) {}
void ProjectWorkspace::setRoot(fs::path root) { root_ = std::move(root); behaviors_.clear(); }
void ProjectWorkspace::validateClass(const std::string& name) {
    if (!classIdentifier(name)) throw std::runtime_error("Behavior name must be a C++ identifier (letters, digits, underscores; cannot start with digit).");
}
void ProjectWorkspace::initialize() {
    if (root_.empty()) throw std::runtime_error("No project folder selected");
    fs::create_directories(scriptsDir());
    fs::create_directories(root_ / "assets");
    if (!fs::exists(root_ / "CMakeLists.txt")) writeText(root_ / "CMakeLists.txt", projectCmake);
    if (!fs::exists(root_ / "behaviors.json")) writeText(root_ / "behaviors.json", "{\n  \"version\": 1,\n  \"behaviors\": []\n}\n");
    reload();
}
void ProjectWorkspace::reload() {
    behaviors_.clear();
    std::ifstream file(root_ / "behaviors.json", std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open behaviors.json");
    json doc = json::parse(file);
    if (!doc.contains("behaviors") || !doc["behaviors"].is_array())
        throw std::runtime_error("Invalid behaviors.json: expected a behaviors array");
    for (const auto& entry : doc["behaviors"]) {
        ScriptEntry b;
        b.name = entry.at("name").get<std::string>();
        b.file = entry.at("file").get<std::string>();
        b.description = entry.value("description", std::string{});
        validateClass(b.name);
        if (!validCodeFile(b.file) || fs::path(b.file).extension() != ".cpp")
            throw std::runtime_error("Invalid script file in manifest: " + b.file);
        if (!fs::is_regular_file(codePath(b.file)))
            throw std::runtime_error("Manifest references a missing script: " + b.file);
        for (const auto& setting : entry.value("settings", json::array())) {
            b.settings.push_back({setting.at("name").get<std::string>(),
                setting.value("default", 0.f), setting.value("min", -100000.f),
                setting.value("max", 100000.f), setting.value("step", 1.f)});
        }
        if (std::any_of(behaviors_.begin(), behaviors_.end(), [&](const ScriptEntry& e) { return e.name == b.name; }))
            throw std::runtime_error("Duplicate behavior in manifest: " + b.name);
        behaviors_.push_back(std::move(b));
    }
}
void ProjectWorkspace::writeManifest() const {
    json doc = {{"version", 1}, {"behaviors", json::array()}};
    for (const auto& b : behaviors_) {
        json data = {{"name", b.name}, {"file", b.file}, {"description", b.description}, {"settings", json::array()}};
        for (const auto& s : b.settings)
            data["settings"].push_back({{"name", s.name}, {"default", s.defaultValue}, {"min", s.minimum}, {"max", s.maximum}, {"step", s.step}});
        doc["behaviors"].push_back(std::move(data));
    }
    writeText(root_ / "behaviors.json", doc.dump(2) + "\n");
}
void ProjectWorkspace::populateRegistry(BehaviorRegistry& registry) const {
    for (const auto& b : behaviors_) registry.addMetadata(b.name, b.description, b.settings);
}
fs::path ProjectWorkspace::codePath(const std::string& filename) const {
    if (!validCodeFile(filename)) throw std::runtime_error("Unsafe or unsupported filename: " + filename);
    return scriptsDir() / filename;
}
std::vector<fs::path> ProjectWorkspace::codeFiles() const {
    std::vector<fs::path> files;
    if (fs::exists(scriptsDir())) for (const auto& entry : fs::directory_iterator(scriptsDir())) {
        if (entry.is_regular_file() && validCodeFile(entry.path().filename().string())) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}
void ProjectWorkspace::createCodeFile(const std::string& filename) {
    const auto path = codePath(filename);
    if (fs::exists(path)) throw std::runtime_error("File already exists: " + filename);
    writeText(path, "// " + filename + "\n");
}
void ProjectWorkspace::createBehavior(const std::string& className) {
    validateClass(className);
    if (std::any_of(behaviors_.begin(), behaviors_.end(), [&](const ScriptEntry& e) { return e.name == className; }))
        throw std::runtime_error("Behavior already exists: " + className);
    const std::string filename = className + ".cpp";
    const auto path = codePath(filename);
    if (fs::exists(path)) throw std::runtime_error("File already exists: " + filename);
    std::ostringstream cpp;
    cpp << "#include \"Behaviors.h\"\n\n"
           "// Automatically discoverable by SpriteForge's behavior manifest.\n"
           "class " << className << " final : public forge::Behavior {\n"
           "public:\n"
           "    void onUpdate(forge::BehaviorContext& ctx) override {\n"
           "        // Example: ctx.object.position.x += 100.f * ctx.deltaTime;\n"
           "        (void)ctx;\n"
           "    }\n"
           "};\n\n"
           "void register_" << className << "(forge::BehaviorRegistry& registry,\n"
           "    const std::string& description, const std::vector<forge::FloatSetting>& settings) {\n"
           "    registry.add<" << className << ">(\"" << className << "\", description, settings);\n"
           "}\n";
    writeText(path, cpp.str());
    behaviors_.push_back({className, filename, "Custom behavior: " + className, {}});
    try { writeManifest(); } catch (...) { behaviors_.pop_back(); fs::remove(path); throw; }
}
void ProjectWorkspace::addFloatSetting(const std::string& behaviorName, FloatSetting setting) {
    if (setting.name.empty() || setting.name.size() > 100) throw std::runtime_error("Setting name must be 1-100 characters");
    if (!std::isfinite(setting.defaultValue) || !std::isfinite(setting.minimum) ||
        !std::isfinite(setting.maximum) || !std::isfinite(setting.step) ||
        setting.minimum > setting.maximum || setting.step <= 0.f ||
        setting.defaultValue < setting.minimum || setting.defaultValue > setting.maximum)
        throw std::runtime_error("Invalid setting range or default");
    for (auto& b : behaviors_) if (b.name == behaviorName) {
        for (const auto& s : b.settings) if (s.name == setting.name)
            throw std::runtime_error("Setting already exists: " + setting.name);
        b.settings.push_back(setting);
        try { writeManifest(); } catch (...) { b.settings.pop_back(); throw; }
        return;
    }
    throw std::runtime_error("Unknown behavior: " + behaviorName);
}
void ProjectWorkspace::removeFloatSetting(const std::string& behaviorName, const std::string& settingName) {
    for (auto& b : behaviors_) if (b.name == behaviorName) {
        const auto backup = b.settings;
        b.settings.erase(std::remove_if(b.settings.begin(), b.settings.end(), [&](const FloatSetting& s) {
            return s.name == settingName;
        }), b.settings.end());
        if (backup.size() == b.settings.size()) throw std::runtime_error("Unknown setting: " + settingName);
        try { writeManifest(); } catch (...) { b.settings = backup; throw; }
        return;
    }
    throw std::runtime_error("Unknown behavior: " + behaviorName);
}
void ProjectWorkspace::renameFile(const std::string& oldName, const std::string& newName) {
    const auto oldPath = codePath(oldName), newPath = codePath(newName);
    if (!fs::is_regular_file(oldPath) || fs::exists(newPath)) throw std::runtime_error("Cannot rename file: missing source or target exists");
    if (oldPath == newPath) return;
    fs::rename(oldPath, newPath);
    try {
        for (auto& behavior : behaviors_) if (behavior.file == oldName) {
            if (newPath.extension() != ".cpp") throw std::runtime_error("A behavior must remain a .cpp file");
            behavior.file = newName;
        }
        writeManifest();
    } catch (...) { fs::rename(newPath, oldPath); reload(); throw; }
}
void ProjectWorkspace::deleteFile(const std::string& filename) {
    const auto path = codePath(filename);
    if (!fs::is_regular_file(path)) throw std::runtime_error("File not found: " + filename);
    // Delete through a same-directory staging filename so we can roll back if saving metadata fails.
    const fs::path moved = scriptsDir() / (".spriteforge-delete-" + filename);
    if (fs::exists(moved)) throw std::runtime_error("Staged deletion already exists");
    fs::rename(path, moved);
    try {
        behaviors_.erase(std::remove_if(behaviors_.begin(), behaviors_.end(), [&](const ScriptEntry& e) { return e.file == filename; }), behaviors_.end());
        writeManifest();
        fs::remove(moved);
    } catch (...) { fs::rename(moved, path); reload(); throw; }
}
void ProjectWorkspace::writeGeneratedRegistry() const {
    std::ostringstream cpp;
    cpp << "// Generated by SpriteForge. Do not edit; changes will be overwritten.\n"
           "#include \"Behaviors.h\"\n\n";
    for (const auto& b : behaviors_) {
        cpp << "void register_" << b.name << "(forge::BehaviorRegistry&, const std::string&, const std::vector<forge::FloatSetting>&);\n";
    }
    cpp << "\nvoid registerGameBehaviors(forge::BehaviorRegistry& registry) {\n";
    for (const auto& b : behaviors_) {
        cpp << "    register_" << b.name << "(registry, " << escapeCpp(b.description) << ", {";
        for (size_t i = 0; i < b.settings.size(); ++i) {
            const auto& s = b.settings[i];
            if (i) cpp << ", ";
            cpp << "{" << escapeCpp(s.name) << ", " << floatLiteral(s.defaultValue) << ", " << floatLiteral(s.minimum) << ", " << floatLiteral(s.maximum) << ", " << floatLiteral(s.step) << "}";
        }
        cpp << "});\n";
    }
    cpp << "}\n";
    writeText(scriptsDir() / ".generated" / "GameBehaviors.cpp", cpp.str());
}
} // namespace forge
