#pragma once
#include <filesystem>
#include <atomic>
#include <thread>
#include <string>
#include "SteamProject.h"

namespace forge {
namespace fs = std::filesystem;

// Compiles a *game project* on a worker thread. Never rebuilds Editor.exe.
class BuildRunner {
public:
    ~BuildRunner();
    bool start(const fs::path& project, const fs::path& sdk, const SteamProject& steam);
    bool busy() const { return busy_.load(); }
    bool takeResult(bool& succeeded);
    const fs::path& logPath() const { return log_; }
    static fs::path runtimePath(const fs::path& project);

private:
    std::thread thread_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> success_{false};
    fs::path log_;
};

} // namespace forge
