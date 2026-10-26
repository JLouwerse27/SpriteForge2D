#pragma once
#include "Behaviors.h"
#include <filesystem>
#include <string>
#include <vector>

namespace forge {

struct ScriptEntry {
    std::string name;
    std::string file; // a single filename in scripts/, not an arbitrary path
    std::string description;
    std::vector<FloatSetting> settings;
};

class ProjectWorkspace {
public:
    explicit ProjectWorkspace(fs::path root = {});
    void setRoot(fs::path root);
    const fs::path& root() const { return root_; }
    fs::path scriptsDir() const { return root_ / "scripts"; }
    const std::vector<ScriptEntry>& behaviors() const { return behaviors_; }
    void initialize();
    void reload();
    void writeManifest() const;
    void writeGeneratedRegistry() const;
    void populateRegistry(BehaviorRegistry& registry) const;
    std::vector<fs::path> codeFiles() const;
    fs::path codePath(const std::string& filename) const;
    void createCodeFile(const std::string& filename);
    void createBehavior(const std::string& className);
    void addFloatSetting(const std::string& behaviorName, FloatSetting setting);
    void removeFloatSetting(const std::string& behaviorName, const std::string& settingName);
    void renameFile(const std::string& oldName, const std::string& newName);
    void deleteFile(const std::string& filename);

private:
    fs::path root_;
    std::vector<ScriptEntry> behaviors_;
    static void validateClass(const std::string& name);
};

} // namespace forge
