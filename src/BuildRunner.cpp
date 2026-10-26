#include "BuildRunner.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace forge {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring quoted(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

int runCommand(const std::wstring& command, const fs::path& workingDir, HANDLE logFile) {
    std::wstring cmd = command;
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdOutput = logFile;
    start.hStdError = logFile;
    // GUI programs often have no valid stdin. Give the child a readable NUL handle.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    Handle stdinNull(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (stdinNull.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Could not open NUL for compiler input");
    start.hStdInput = stdinNull.value;
    PROCESS_INFORMATION proc{};
    const std::wstring cwd = workingDir.wstring();
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, cwd.c_str(), &start, &proc))
        throw std::runtime_error("Could not start CMake (is CMake installed and on PATH?)");
    Handle process(proc.hProcess);
    Handle thread(proc.hThread);
    WaitForSingleObject(proc.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(proc.hProcess, &exitCode);
    return static_cast<int>(exitCode);
}

void appendLog(HANDLE h, const std::string& text) {
    DWORD written = 0;
    WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}
}
BuildRunner::~BuildRunner() { if (thread_.joinable()) thread_.join(); }
fs::path BuildRunner::runtimePath(const fs::path& project) {
    return project / ".spriteforge-build" / "bin" / "Release" / "GameRuntime.exe";
}
bool BuildRunner::start(const fs::path& project, const fs::path& sdk, const SteamProject& steam) {
    if (busy_.load()) return false;
    if (thread_.joinable()) thread_.join();
    ready_ = false;
    success_ = false;
    busy_ = true;
    log_ = project / ".spriteforge-build" / "build.log";
    try {
        thread_ = std::thread([this, project, sdk, steam] {
            try {
                fs::create_directories(log_.parent_path());
                SECURITY_ATTRIBUTES sa{};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = TRUE;
                Handle log(CreateFileW(log_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (log.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open build log");
                appendLog(log.value, "Configuring the game project...\r\n");
                const fs::path build = project / ".spriteforge-build";
                std::wstring configure = L"cmake -S " + quoted(project) + L" -B " + quoted(build) +
                    L" -G \"Visual Studio 17 2022\" -A x64 -DSPRITEFORGE_SDK:PATH=" + quoted(sdk);
                configure += L" -DSPRITEFORGE_STEAM_ENABLED:BOOL=" +
                    std::wstring(steam.enabled ? L"ON" : L"OFF");
                if (steam.enabled) {
                    if (steam.appId == 0) throw std::runtime_error("Steam enabled but App ID is zero");
                    if (!fs::exists(steam.sdkPath / "public" / "steam" / "steam_api.h"))
                        throw std::runtime_error("Steam enabled but SDK path is missing public/steam/steam_api.h");
                    configure += L" -DSPRITEFORGE_STEAM_SDK:PATH=" + quoted(fs::absolute(steam.sdkPath));
                }
                int result = runCommand(configure, project, log.value);
                if (result == 0) {
                    appendLog(log.value, "\r\nCompiling game code...\r\n");
                    const std::wstring compile = L"cmake --build " + quoted(build) +
                        L" --config Release --target GameRuntime --parallel 4";
                    result = runCommand(compile, project, log.value);
                }
                if (result != 0) appendLog(log.value, "\r\nBuild failed. Review the compiler errors above.\r\n");
                else if (!fs::is_regular_file(runtimePath(project))) {
                    appendLog(log.value, "\r\nGameRuntime.exe not found after build.\r\n");
                    result = 1;
                } else {
                    const fs::path exeDir = runtimePath(project).parent_path();
                    // For local development only. NEVER copy this file to Steam release exports.
                    const fs::path devAppID = exeDir / "steam_appid.txt";
                    if (steam.enabled) {
                        Handle appid(CreateFileW(devAppID.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
                        if (appid.value == INVALID_HANDLE_VALUE)
                            throw std::runtime_error("Could not create development steam_appid.txt");
                        appendLog(appid.value, std::to_string(steam.appId) + "\n");
                    } else {
                        std::error_code ec;
                        fs::remove(devAppID, ec);
                        fs::remove(exeDir / "steam_api64.dll", ec);
                    }
                    appendLog(log.value, "\r\nBuild succeeded.\r\n");
                }
                success_ = result == 0;
            } catch (const std::exception& e) {
                success_ = false;
                // If the build log could not be opened, create a fallback log.
                try {
                    Handle log(CreateFileW(log_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
                    if (log.value != INVALID_HANDLE_VALUE) appendLog(log.value, std::string("\r\n") + e.what() + "\r\n");
                } catch (...) {}
            }
            busy_ = false;
            ready_ = true;
        });
    } catch (...) { busy_ = false; throw; }
    return true;
}
bool BuildRunner::takeResult(bool& succeeded) {
    if (!ready_.exchange(false)) return false;
    if (thread_.joinable()) thread_.join();
    succeeded = success_.load();
    return true;
}
} // namespace forge
