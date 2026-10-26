#include "EngineCore.h"
#include "Behaviors.h"
#include "ProjectWorkspace.h"
#include "BuildRunner.h"
#include <imgui.h>
#include <imgui-SFML.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <cfloat>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace forge;

namespace {

fs::path chooseFile(bool save, HWND owner = nullptr) {
    wchar_t filename[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Scene files (*.json)\0*.json\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? fs::path(filename) : fs::path{};
}

fs::path chooseImage(HWND owner = nullptr) {
    wchar_t filename[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(filename) : fs::path{};
}

fs::path chooseFolder(HWND owner = nullptr, const wchar_t* title = L"Choose a folder") {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (!item) return {};
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    return ok ? fs::path(path) : fs::path{};
}

std::string safeGameName(const char* value) {
    std::string name;
    for (unsigned char c : std::string(value)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') name += static_cast<char>(c);
        else if (c == ' ') name += '_';
    }
    return name.empty() ? "MyGame" : name.substr(0, 60);
}

bool imageExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
}

std::vector<fs::path> assetsIn(const fs::path& root) {
    std::vector<fs::path> items;
    const fs::path directory = root / "assets";
    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory))
            if (entry.is_regular_file() && imageExtension(entry.path())) items.push_back(entry.path());
    }
    std::sort(items.begin(), items.end());
    return items;
}

// Copy the full assets directory: C++ behaviors can spawn sprites whose
// image paths are not referenced by objects in the saved scene.
void copyProjectAssets(const Scene& scene, const fs::path& oldRoot, const fs::path& newRoot) {
    const fs::path srcRoot = fs::absolute(oldRoot / "assets").lexically_normal();
    const fs::path dstRoot = fs::absolute(newRoot / "assets").lexically_normal();
    for (const auto& object : scene.objects) {
        const fs::path rel = fs::path(object.texture).lexically_normal();
        if (rel.empty() || rel.is_absolute() || *rel.begin() != fs::path("assets") ||
            object.texture.find("..") != std::string::npos)
            throw std::runtime_error("Invalid relative sprite path: " + object.texture);
        if (!fs::is_regular_file(oldRoot / rel))
            throw std::runtime_error("Missing sprite: " + (oldRoot / rel).string());
    }
    if (!fs::exists(srcRoot)) return;
    if (srcRoot == dstRoot) return;
    // Do not recursively include an export folder created inside assets/.
    const auto srcStr = srcRoot.generic_string() + "/";
    const auto dstStr = dstRoot.generic_string() + "/";
    if (dstStr.compare(0, srcStr.size(), srcStr) == 0)
        throw std::runtime_error("Cannot export/save project inside its own assets directory");

    for (const auto& entry : fs::recursive_directory_iterator(srcRoot)) {
        if (entry.is_symlink())
            throw std::runtime_error("Symlink in assets folder is not supported: " + entry.path().string());
        if (!entry.is_regular_file()) continue;
        const fs::path rel = entry.path().lexically_relative(srcRoot);
        const fs::path dst = dstRoot / rel;
        fs::create_directories(dst.parent_path());
        fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing);
    }
}

void drawPanelTitle(const char* title) {
    ImGui::TextUnformatted(title);
    ImGui::Separator();
}

class EditorApp {
    sf::RenderWindow window_;
    sf::RenderTexture viewport_{{800u, 450u}};
    TextureCache textures_;
    BehaviorRegistry registry_;
    ProjectWorkspace workspace_;
    BuildRunner builder_;
    Scene scene_;
    fs::path projectDir_;
    fs::path currentSceneFile_;
    std::string status_ = "Ready. Import a PNG or drag a sprite from Assets into the viewport.";
    int selectedId_ = -1;
    int inspectorId_ = -1;
    char objectName_[256] = {};
    char exportName_[128] = "MyGame";
    HANDLE gameProcess_ = nullptr;
    enum class AfterBuild { None, Play, Export };
    AfterBuild afterBuild_ = AfterBuild::None;
    fs::path exportDestination_;
    std::string selectedCodeFile_;
    std::vector<char> sourceBuffer_ = std::vector<char>(1024 * 1024, '\0');
    bool sourceDirty_ = false;
    char newCodeName_[128] = {};
    char renameCodeName_[128] = {};
    char newSettingName_[128] = {};
    FloatSetting newSetting_{};
    std::string deleteCodeName_;
    bool codeExpanded_ = false;
    bool draggingObject_ = false;
    sf::Vector2f objectDragOffset_{0.f, 0.f};
    sf::Vector2f camera_{480.f, 270.f};
    float zoom_ = 1.f;
    float viewportW_ = 800.f;
    float viewportH_ = 450.f;
    bool grid_ = true;
    bool showAbout_ = false;
    bool showScriptHelp_ = false;

public:
    EditorApp()
        : window_(sf::VideoMode({1440u, 860u}), "SpriteForge 2D - SFML Scene Editor"),
          projectDir_(executableDirectory() / "Project"),
          currentSceneFile_(projectDir_ / "scene.json") {
        workspace_.setRoot(projectDir_);
        workspace_.initialize();
        reloadRegistry();
        window_.setVerticalSyncEnabled(true);
        if (!fs::exists(currentSceneFile_)) {
            fs::create_directories(projectDir_ / "assets");
            currentSceneFile_.clear();
        } else {
            std::string err;
            if (!loadScene(scene_, currentSceneFile_, err)) status_ = "Scene load error: " + err;
        }
        camera_ = {scene_.width / 2.f, scene_.height / 2.f};
        std::strncpy(exportName_, safeGameName(scene_.title.c_str()).c_str(), sizeof(exportName_)-1);
        window_.setKeyRepeatEnabled(true);
    }

    void run() {
        if (!ImGui::SFML::Init(window_)) throw std::runtime_error("Failed to initialize ImGui-SFML");
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 3.f;
        ImGui::GetStyle().FrameRounding = 3.f;
        sf::Clock clock;
        while (window_.isOpen()) {
            while (auto event = window_.pollEvent()) {
                ImGui::SFML::ProcessEvent(window_, *event);
                if (event->is<sf::Event::Closed>()) window_.close();
            }
            const float dt = std::min(clock.restart().asSeconds(), 0.05f);
            ImGui::SFML::Update(window_, sf::seconds(dt));
            (void)dt;
            bool built = false;
            if (builder_.takeResult(built)) {
                notify(built ? "Game build succeeded." : "Build failed. Check the Build Output tab.");
                if (built) {
                    try {
                        if (afterBuild_ == AfterBuild::Play) launchGame();
                        else if (afterBuild_ == AfterBuild::Export) exportBuiltGame();
                    } catch (const std::exception& e) { notify(std::string("After build: ") + e.what()); }
                }
                afterBuild_ = AfterBuild::None;
            }
            if (gameProcess_ && WaitForSingleObject(gameProcess_, 0) == WAIT_OBJECT_0) {
                CloseHandle(gameProcess_);
                gameProcess_ = nullptr;
                notify("Game exited.");
            }
            menuBar();
            mainPanels();
            if (showAbout_) {
                ImGui::Begin("About SpriteForge", &showAbout_);
                ImGui::TextWrapped("SpriteForge 2D is a minimal visual scene editor written with SFML 3 and Dear ImGui. Exported games run in a separate native Windows executable.");
                ImGui::TextWrapped("The editor is independent of game code. Each project compiles its C++ behaviors into its own GameRuntime.exe.");
                ImGui::End();
            }
            if (showScriptHelp_) {
                ImGui::Begin("C++ game behaviors", &showScriptHelp_);
                ImGui::TextWrapped("Use Code > New behavior to generate a C++ class in PROJECT/scripts/.");
                ImGui::TextWrapped("Every script in behaviors.json is shown in the Inspector. Edit its description and float properties in behaviors.json (then use Reload behaviors).");
                ImGui::TextWrapped("Build, Play, and Export automatically compile the current game project. The editor itself is never rebuilt for script changes.");
                ImGui::TextWrapped("Use the Build Output tab to view compiler errors. Play launches a separate game window. You can close it with Escape.");
                ImGui::End();
            }
            window_.clear(sf::Color(18, 20, 25));
            ImGui::SFML::Render(window_);
            window_.display();
        }
        if (gameProcess_) { CloseHandle(gameProcess_); gameProcess_ = nullptr; }
        ImGui::SFML::Shutdown();
    }

private:
    void reloadRegistry() {
        registry_ = BehaviorRegistry{};
        workspace_.reload();
        workspace_.populateRegistry(registry_);
    }

    // Internal C++ source editor (no autocomplete or debugger).
    bool saveCode() {
        if (!sourceDirty_ || selectedCodeFile_.empty()) return true;
        try {
            std::ofstream out(workspace_.codePath(selectedCodeFile_), std::ios::binary | std::ios::trunc);
            if (!out || !(out << sourceBuffer_.data())) throw std::runtime_error("Unable to save script");
            sourceDirty_ = false;
            notify("Saved " + selectedCodeFile_);
            return true;
        } catch (const std::exception& e) { notify(e.what()); return false; }
    }

    void loadCode(const std::string& filename) {
        if (filename == selectedCodeFile_) return;
        if (!saveCode()) return;
        try {
            std::ifstream in(workspace_.codePath(filename), std::ios::binary);
            if (!in) throw std::runtime_error("Cannot open script: " + filename);
            const std::string text((std::istreambuf_iterator<char>(in)), {});
            if (text.size() >= sourceBuffer_.size() - 1)
                throw std::runtime_error("Script exceeds 1 MB editor limit: " + filename);
            std::fill(sourceBuffer_.begin(), sourceBuffer_.end(), '\0');
            std::copy(text.begin(), text.end(), sourceBuffer_.begin());
            selectedCodeFile_ = filename;
            sourceDirty_ = false;
        } catch (const std::exception& e) { notify(e.what()); }
    }

    bool canSwitchProject() {
        if (builder_.busy()) { notify("Wait for the current build to finish before switching projects."); return false; }
        return saveCode();
    }

    void openProject() {
        if (!canSwitchProject()) return;
        const fs::path dest = chooseFolder(window_.getNativeHandle(), L"Select an existing SpriteForge game project");
        if (dest.empty()) return;
        try {
            if (!fs::exists(dest / "scene.json")) throw std::runtime_error("No scene.json in selected folder");
            Scene loaded;
            std::string err;
            if (!loadScene(loaded, dest / "scene.json", err)) throw std::runtime_error(err);
            ProjectWorkspace newWorkspace(dest);
            newWorkspace.initialize();
            scene_ = std::move(loaded);
            workspace_ = std::move(newWorkspace);
            projectDir_ = dest;
            currentSceneFile_ = dest / "scene.json";
            reloadRegistry();
            selectedCodeFile_.clear(); sourceDirty_ = false;
            selectedId_ = inspectorId_ = -1;
            textures_.clear();
            notify("Opened project " + dest.string());
        } catch (const std::exception& e) { notify(std::string("Open project failed: ") + e.what()); }
    }

    void newProject() {
        if (!canSwitchProject()) return;
        const fs::path dest = chooseFolder(window_.getNativeHandle(), L"Select a folder for the NEW game project");
        if (dest.empty()) return;
        if (fs::exists(dest / "scene.json")) { notify("Existing scene.json found. Use Open project instead."); return; }
        try {
            ProjectWorkspace newWorkspace(dest);
            newWorkspace.initialize();
            Scene newScene;
            std::string err;
            if (!saveScene(newScene, dest / "scene.json", err)) throw std::runtime_error(err);
            scene_ = std::move(newScene);
            workspace_ = std::move(newWorkspace);
            projectDir_ = dest;
            currentSceneFile_ = dest / "scene.json";
            reloadRegistry();
            selectedCodeFile_.clear(); sourceDirty_ = false;
            selectedId_ = inspectorId_ = -1;
            textures_.clear();
            camera_ = {scene_.width / 2.f, scene_.height / 2.f};
            notify("Created project " + dest.string());
        } catch (const std::exception& e) { notify(std::string("New project failed: ") + e.what()); }
    }

    void stopGame() {
        if (!gameProcess_) return;
        DWORD pid = GetProcessId(gameProcess_);
        EnumWindows([](HWND hwnd, LPARAM param)->BOOL {
            DWORD wndPid = 0;
            GetWindowThreadProcessId(hwnd, &wndPid);
            if (wndPid == static_cast<DWORD>(param)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return TRUE;
        }, static_cast<LPARAM>(pid));
        notify("Requested game window to close.");
    }

    void launchGame() {
        const fs::path exe = BuildRunner::runtimePath(projectDir_);
        if (!fs::is_regular_file(exe)) throw std::runtime_error("Compiled runtime is missing");
        if (gameProcess_) { stopGame(); throw std::runtime_error("Close the previous game window before pressing Play again"); }
        const std::wstring command = L"\"" + exe.wstring() + L"\" --scene \"" + currentSceneFile_.wstring() + L"\"";
        std::wstring writable = command;
        STARTUPINFOW start{}; start.cb = sizeof(start);
        PROCESS_INFORMATION proc{};
        const std::wstring cwd = projectDir_.wstring();
        if (!CreateProcessW(exe.c_str(), writable.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, cwd.c_str(), &start, &proc)) throw std::runtime_error("Could not launch game executable");
        gameProcess_ = proc.hProcess;
        CloseHandle(proc.hThread);
        notify("Game launched. Press Escape in the game window to quit.");
    }

    void requestBuild(AfterBuild action = AfterBuild::None) {
        if (builder_.busy()) { notify("Build already running."); return; }
        if (gameProcess_ && WaitForSingleObject(gameProcess_, 0) != WAIT_OBJECT_0) {
            notify("Close the running game before rebuilding (Windows locks running EXE files)."); return;
        }
        if (!saveCode()) return;
        if (currentSceneFile_.empty()) currentSceneFile_ = projectDir_ / "scene.json";
        std::string err;
        if (!saveScene(scene_, currentSceneFile_, err)) { notify("Save failed: " + err); return; }
        try {
            validateBehaviors(scene_, registry_);
            workspace_.writeGeneratedRegistry();
            const fs::path sdk = executableDirectory() / "sdk";
            if (!fs::is_regular_file(sdk / "cmake" / "SpriteForgeGame.cmake"))
                throw std::runtime_error("Missing SDK files. Rebuild/reinstall the SpriteForge editor first.");
            afterBuild_ = action;
            builder_.start(projectDir_, sdk);
            notify("Building game C++... Open the Build Output tab for errors.");
        } catch (const std::exception& e) { afterBuild_ = AfterBuild::None; notify(std::string("Build setup error: ") + e.what()); }
    }

    void exportBuiltGame() {
        const fs::path runtime = BuildRunner::runtimePath(projectDir_);
        if (!fs::is_regular_file(runtime)) throw std::runtime_error("GameRuntime.exe missing after build");
        const std::string name = safeGameName(exportName_);
        const fs::path out = exportDestination_ / name;
        fs::create_directories(out);
        copyProjectAssets(scene_, projectDir_, out);
        std::string err;
        if (!saveScene(scene_, out / "scene.json", err)) throw std::runtime_error(err);
        fs::copy_file(runtime, out / (name + ".exe"), fs::copy_options::overwrite_existing);
        notify("Exported " + (out / (name + ".exe")).string());
    }

    void notify(const std::string& text) { status_ = text; }

    void saveAs() {
        if (!canSwitchProject()) return;
        const fs::path dest = chooseFile(true, window_.getNativeHandle());
        if (dest.empty()) return;
        try {
            if (fs::absolute(dest.parent_path()) != fs::absolute(projectDir_)) {
                throw std::runtime_error("Save as outside the current project is not supported. Use File > New project to create a separate game.");
            }
            currentSceneFile_ = dest;
            std::string err;
            if (!saveScene(scene_, dest, err)) throw std::runtime_error(err);
            notify("Saved scene: " + dest.string());
        } catch (const std::exception& e) { notify(std::string("Save failed: ") + e.what()); }
    }

    void save() {
        if (currentSceneFile_.empty()) currentSceneFile_ = projectDir_ / "scene.json";
        std::string err;
        if (!saveScene(scene_, currentSceneFile_, err)) notify("Save failed: " + err);
        else notify("Saved scene: " + currentSceneFile_.string());
    }

    void open() {
        if (!canSwitchProject()) return;
        const fs::path dest = chooseFile(false, window_.getNativeHandle());
        if (dest.empty()) return;
        Scene loaded;
        std::string err;
        if (!loadScene(loaded, dest, err)) { notify("Open failed: " + err); return; }
        scene_ = std::move(loaded);
        projectDir_ = dest.parent_path();
        currentSceneFile_ = dest;
        workspace_.setRoot(projectDir_);
        try { workspace_.initialize(); reloadRegistry(); }
        catch (const std::exception& e) { notify(std::string("Project metadata: ") + e.what()); }
        selectedCodeFile_.clear(); sourceDirty_ = false;
        textures_.clear();
        selectedId_ = -1;
        inspectorId_ = -1;
        camera_ = {scene_.width/2.f, scene_.height/2.f};
        zoom_ = 1.f;
        notify("Opened: " + dest.string());
    }

    void importImage() {
        const auto source = chooseImage(window_.getNativeHandle());
        if (source.empty()) return;
        try {
            fs::create_directories(projectDir_ / "assets");
            fs::path dest = projectDir_ / "assets" / source.filename();
            if (fs::absolute(source) != fs::absolute(dest)) {
                int i = 1;
                while (fs::exists(dest)) {
                    dest = projectDir_ / "assets" /
                        (source.stem().string() + "_" + std::to_string(i++) + source.extension().string());
                }
                fs::copy_file(source, dest);
            }
            textures_.invalidate(dest);
            notify("Imported " + dest.filename().string() + ". Drag it into the Scene viewport.");
        } catch (const std::exception& e) { notify(std::string("Import failed: ") + e.what()); }
    }

    void exportGame() {
        if (builder_.busy()) { notify("A build is already running."); return; }
        const auto dest = chooseFolder(window_.getNativeHandle(), L"Choose a folder for the exported game");
        if (dest.empty()) return;
        exportDestination_ = dest;
        requestBuild(AfterBuild::Export);
    }

    void menuBar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New project...")) newProject();
                if (ImGui::MenuItem("Open project...")) openProject();
                ImGui::Separator();
                if (ImGui::MenuItem("New scene")) {
                    if (builder_.busy()) notify("Cannot change scenes during a build.");
                    else {
                        scene_ = Scene{};
                        selectedId_ = inspectorId_ = -1;
                        currentSceneFile_ = projectDir_ / "scene.json";
                        camera_ = {480.f, 270.f};
                        notify("New scene. Save to replace the current scene.json.");
                    }
                }
                if (ImGui::MenuItem("Open scene...")) open();
                if (ImGui::MenuItem("Save scene")) save();
                if (ImGui::MenuItem("Save as...")) saveAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Import image...")) importImage();
                ImGui::Separator();
                if (ImGui::MenuItem("Build game", nullptr, false, !builder_.busy())) requestBuild();
                if (ImGui::MenuItem("Play game (build first)", nullptr, false, !builder_.busy())) requestBuild(AfterBuild::Play);
                if (ImGui::MenuItem("Stop game", nullptr, false, gameProcess_ != nullptr)) stopGame();
                if (ImGui::MenuItem("Export EXE...", nullptr, false, !builder_.busy())) exportGame();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Code")) {
                if (ImGui::MenuItem("Save C++ file")) saveCode();
                if (ImGui::MenuItem("Reload behaviors.json")) {
                    try { reloadRegistry(); notify("Reloaded behavior metadata."); }
                    catch (const std::exception& e) { notify(e.what()); }
                }
                ImGui::MenuItem("Expand code panel", nullptr, &codeExpanded_);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Grid", nullptr, &grid_);
                if (ImGui::MenuItem("Center on scene")) camera_ = {scene_.width/2.f, scene_.height/2.f};
                if (ImGui::MenuItem("Reset zoom")) zoom_ = 1.f;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) showAbout_ = true;
                if (ImGui::MenuItem("C++ scripting guide")) showScriptHelp_ = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void mainPanels() {
        const auto size = ImGui::GetIO().DisplaySize;
        const float top = ImGui::GetFrameHeight() + 4.f;
        const float bottom = 26.f;
        const float leftWidth = std::max(175.f, size.x * 0.18f);
        const float rightWidth = std::max(205.f, size.x * 0.22f);
        const float assetHeight = codeExpanded_ ? std::max(260.f, size.y * 0.65f)
            : std::clamp(size.y * 0.35f, 220.f, 380.f);
        const float centerW = std::max(100.f, size.x-leftWidth-rightWidth);
        const float centerH = std::max(100.f, size.y-top-assetHeight-bottom);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos({0.f, top});
        ImGui::SetNextWindowSize({leftWidth, size.y-top-bottom});
        ImGui::Begin("Hierarchy", nullptr, flags);
        drawPanelTitle("Objects");
        ImGui::BeginDisabled(builder_.busy());
        if (ImGui::Button("+ Sprite")) {
            const auto all = assetsIn(projectDir_);
            if (all.empty()) notify("Import an image into the Assets panel first.");
            else addSprite("assets/" + all.front().filename().string(), camera_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete") && selectedId_ >= 0) {
            scene_.remove(selectedId_); selectedId_ = -1;
        }
        ImGui::Separator();
        for (const auto& object : scene_.objects) {
            ImGui::PushID(object.id);
            if (ImGui::Selectable(object.name.c_str(), selectedId_ == object.id)) selectedId_ = object.id;
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("OBJECT_ID", &object.id, sizeof(object.id));
                ImGui::TextUnformatted(object.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("OBJECT_ID")) {
                    const int from = *static_cast<const int*>(payload->Data);
                    auto a = std::find_if(scene_.objects.begin(), scene_.objects.end(),
                                          [from](const Object& o){return o.id == from;});
                    auto b = std::find_if(scene_.objects.begin(), scene_.objects.end(),
                                          [id=object.id](const Object& o){return o.id == id;});
                    if (a != scene_.objects.end() && b != scene_.objects.end()) std::iter_swap(a,b);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::End();

        ImGui::SetNextWindowPos({size.x-rightWidth, top});
        ImGui::SetNextWindowSize({rightWidth, size.y-top-bottom});
        ImGui::Begin("Inspector", nullptr, flags);
        inspector();
        ImGui::End();

        ImGui::SetNextWindowPos({leftWidth,top});
        ImGui::SetNextWindowSize({centerW,centerH});
        ImGui::Begin("Scene", nullptr, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        sceneViewport();
        ImGui::End();

        ImGui::SetNextWindowPos({leftWidth,top+centerH});
        ImGui::SetNextWindowSize({centerW,assetHeight});
        ImGui::Begin("Assets / Code / Build", nullptr, flags);
        if (ImGui::BeginTabBar("WorkspaceTabs")) {
            if (ImGui::BeginTabItem("Assets")) { assetBrowser(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Code")) { codePanel(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Build Output")) { buildOutputPanel(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::SetNextWindowPos({0.f,size.y-bottom});
        ImGui::SetNextWindowSize({size.x,bottom});
        ImGui::Begin("Status", nullptr, flags | ImGuiWindowFlags_NoTitleBar);
        ImGui::TextUnformatted(status_.c_str());
        ImGui::End();
    }

    void inspector() {
        Object* obj = scene_.find(selectedId_);
        if (!obj) {
            drawPanelTitle("Scene properties");
            char title[128] = {};
            std::strncpy(title, scene_.title.c_str(), sizeof(title)-1);
            if (ImGui::InputText("Title", title, sizeof(title))) scene_.title = title;
            int dimensions[] = {static_cast<int>(scene_.width), static_cast<int>(scene_.height)};
            if (ImGui::InputInt2("Size", dimensions)) {
                scene_.width = std::clamp(dimensions[0], 160, 3840);
                scene_.height = std::clamp(dimensions[1], 120, 2160);
            }
            float background[] = {scene_.background.r/255.f,scene_.background.g/255.f,scene_.background.b/255.f};
            if (ImGui::ColorEdit3("Background", background))
                scene_.background = sf::Color(static_cast<std::uint8_t>(background[0]*255),
                                             static_cast<std::uint8_t>(background[1]*255),
                                             static_cast<std::uint8_t>(background[2]*255));
            ImGui::TextWrapped("Select a sprite in the hierarchy or viewport to edit its properties.");
            return;
        }
        drawPanelTitle("Sprite properties");
        if (inspectorId_ != obj->id) {
            inspectorId_ = obj->id;
            std::memset(objectName_, 0, sizeof(objectName_));
            std::strncpy(objectName_, obj->name.c_str(), sizeof(objectName_)-1);
        }
        if (ImGui::InputText("Name", objectName_, sizeof(objectName_))) obj->name = objectName_;
        ImGui::DragFloat2("Position", &obj->position.x, 1.f);
        ImGui::DragFloat2("Scale", &obj->scale.x, 0.01f, 0.01f, 100.f);
        ImGui::DragFloat("Rotation", &obj->rotation, 1.f, -360.f, 360.f);
        ImGui::Checkbox("Visible", &obj->visible);
        if (obj->behavior.empty()) {
            ImGui::Checkbox("Controllable (legacy)", &obj->controllable);
            if (obj->controllable) ImGui::DragFloat("Speed", &obj->moveSpeed, 1.f, 0.f, 3000.f);
        }
        ImGui::Separator();
        ImGui::TextUnformatted("C++ Behavior (compiled into game EXE)");
        const char* currentBehavior = obj->behavior.empty() ? "None" : obj->behavior.c_str();
        if (ImGui::BeginCombo("Behavior", currentBehavior)) {
            const bool none = obj->behavior.empty();
            if (ImGui::Selectable("None", none)) {
                obj->behavior.clear();
                obj->behaviorValues.clear();
            }
            for (const auto& def : registry_.definitions()) {
                const bool selected = obj->behavior == def.name;
                if (ImGui::Selectable(def.name.c_str(), selected)) {
                    obj->behavior = def.name;
                    obj->behaviorValues.clear();
                    for (const auto& setting : def.settings)
                        obj->behaviorValues[setting.name] = setting.defaultValue;
                    obj->controllable = false;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!obj->behavior.empty()) {
            if (const auto* def = registry_.find(obj->behavior)) {
                ImGui::TextDisabled("%s", def->description.c_str());
                for (const auto& setting : def->settings) {
                    auto [it, inserted] = obj->behaviorValues.try_emplace(setting.name, setting.defaultValue);
                    (void)inserted;
                    ImGui::DragFloat(setting.name.c_str(), &it->second,
                                     setting.step, setting.minimum, setting.maximum);
                }
            } else {
                ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "Unknown behavior: %s", obj->behavior.c_str());
                ImGui::TextWrapped("Create its C++ source or add it to behaviors.json and reload metadata.");
            }
        }
        if (ImGui::SmallButton("Where do I write C++ behaviors?")) showScriptHelp_ = true;
        ImGui::Separator();
        ImGui::TextWrapped("Texture: %s", obj->texture.c_str());
        if (const auto* tex = textures_.get(projectDir_ / obj->texture)) {
            const auto s = tex->getSize();
            const float ratio = std::min(140.f/std::max(1u,s.x), 100.f/std::max(1u,s.y));
            ImGui::Image(*tex, {s.x*ratio,s.y*ratio});
        }
        if (ImGui::Button("Duplicate")) {
            const auto copy = *obj;
            auto& newObj = scene_.add(copy.name+" Copy", copy.texture,
                                      {copy.position.x+24, copy.position.y+24});
            newObj.scale = copy.scale;
            newObj.rotation = copy.rotation;
            newObj.visible = copy.visible;
            newObj.controllable = copy.controllable;
            newObj.moveSpeed = copy.moveSpeed;
            newObj.behavior = copy.behavior;
            newObj.behaviorValues = copy.behaviorValues;
            selectedId_ = newObj.id;
        }
    }

    void addSprite(const std::string& rel, sf::Vector2f pos) {
        const fs::path filename(rel);
        auto& obj = scene_.add(filename.stem().string(), rel, pos);
        selectedId_ = obj.id;
        inspectorId_ = -1;
        notify("Added sprite: " + rel);
    }

    void assetBrowser() {
        if (ImGui::Button("Import PNG / JPG...")) importImage();
        ImGui::SameLine();
        ImGui::TextDisabled("Drag a thumbnail into Scene to create an object");
        ImGui::Separator();
        const auto all = assetsIn(projectDir_);
        if (all.empty()) { ImGui::TextDisabled("No images. Import one to start."); return; }
        ImGui::BeginDisabled(builder_.busy());
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            const auto& path = all[i];
            ImGui::PushID(i);
            const auto* tex = textures_.get(path);
            if (tex) {
                const auto s = tex->getSize();
                const float factor = std::min(64.f/std::max(1u,s.x),64.f/std::max(1u,s.y));
                ImGui::Image(*tex, {s.x*factor,s.y*factor});
                if (ImGui::BeginDragDropSource()) {
                    const std::string rel = "assets/" + path.filename().string();
                    ImGui::SetDragDropPayload("SPRITE_ASSET", rel.c_str(), rel.size()+1);
                    ImGui::Text("Place %s", path.filename().string().c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    addSprite("assets/" + path.filename().string(), camera_);
            } else {
                ImGui::Button("Invalid", {64,64});
            }
            ImGui::TextWrapped("%s", path.filename().string().c_str());
            if (i+1 < static_cast<int>(all.size())) ImGui::SameLine(0.f, 12.f);
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }

    void codePanel() {
        ImGui::BeginDisabled(builder_.busy());
        if (ImGui::Button("New behavior...")) { newCodeName_[0] = 0; ImGui::OpenPopup("New behavior"); }
        ImGui::SameLine();
        if (ImGui::Button("New file...")) { newCodeName_[0] = 0; ImGui::OpenPopup("New C++ file"); }
        ImGui::SameLine();
        if (ImGui::Button("Save file")) saveCode();
        ImGui::SameLine();
        if (ImGui::Button("Build")) requestBuild();
        ImGui::SameLine();
        if (ImGui::Button("Play")) requestBuild(AfterBuild::Play);
        ImGui::SameLine();
        if (ImGui::SmallButton(codeExpanded_ ? "Shrink" : "Expand")) codeExpanded_ = !codeExpanded_;
        if (ImGui::BeginPopupModal("New behavior", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Class name", newCodeName_, sizeof(newCodeName_));
            if (ImGui::Button("Create")) {
                try {
                    if (!saveCode()) throw std::runtime_error("Save the current file first");
                    workspace_.createBehavior(newCodeName_);
                    reloadRegistry();
                    loadCode(std::string(newCodeName_) + ".cpp");
                    notify("Created C++ behavior " + std::string(newCodeName_));
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) { notify(e.what()); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("New C++ file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Filename (.cpp/.h/.hpp)", newCodeName_, sizeof(newCodeName_));
            if (ImGui::Button("Create")) {
                try {
                    if (!saveCode()) throw std::runtime_error("Save the current file first");
                    workspace_.createCodeFile(newCodeName_);
                    loadCode(newCodeName_);
                    notify("Created " + std::string(newCodeName_));
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) { notify(e.what()); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();
        ImGui::BeginChild("Files", ImVec2(182.f, 0.f), true);
        ImGui::TextUnformatted("Project / scripts");
        ImGui::Separator();
        bool openRename = false, openDelete = false;
        for (const auto& entry : workspace_.codeFiles()) {
            const std::string name = entry.filename().string();
            ImGui::PushID(name.c_str());
            if (ImGui::Selectable(name.c_str(), selectedCodeFile_ == name)) loadCode(name);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename...")) {
                    std::strncpy(renameCodeName_, name.c_str(), sizeof(renameCodeName_)-1);
                    deleteCodeName_ = name;
                    openRename = true;
                }
                if (ImGui::MenuItem("Delete...")) {
                    deleteCodeName_ = name;
                    openDelete = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (openRename) ImGui::OpenPopup("Rename script");
        if (openDelete) ImGui::OpenPopup("Delete script");
        // Keep popups here (outside each file's PushID context).
        if (ImGui::BeginPopupModal("Rename script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Rename %s", deleteCodeName_.c_str());
            ImGui::InputText("New filename", renameCodeName_, sizeof(renameCodeName_));
            if (ImGui::Button("Rename")) {
                if (saveCode()) try {
                    const std::string previous = deleteCodeName_;
                    workspace_.renameFile(previous, renameCodeName_);
                    if (selectedCodeFile_ == previous) selectedCodeFile_ = renameCodeName_;
                    reloadRegistry();
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) { notify(e.what()); }
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Permanently delete %s? This may cause compile errors in files that reference it.", deleteCodeName_.c_str());
            if (ImGui::Button("Delete file")) {
                if (saveCode()) try {
                    workspace_.deleteFile(deleteCodeName_);
                    if (selectedCodeFile_ == deleteCodeName_) { selectedCodeFile_.clear(); sourceDirty_ = false; }
                    reloadRegistry();
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) { notify(e.what()); }
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::BeginChild("SourceEditor", ImVec2(0,0), false);
        std::string currentBehavior;
        for (const auto& entry : workspace_.behaviors())
            if (entry.file == selectedCodeFile_) { currentBehavior = entry.name; break; }
        if (!currentBehavior.empty()) {
            const auto* def = registry_.find(currentBehavior);
            ImGui::Text("Behavior: %s", currentBehavior.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Float property")) {
                newSettingName_[0] = 0;
                newSetting_ = {"", 0.f, 0.f, 1000.f, 1.f};
                ImGui::OpenPopup("Add float setting");
            }
            if (def && !def->settings.empty()) {
                for (const auto& setting : def->settings) {
                    ImGui::PushID(setting.name.c_str());
                    ImGui::Text("%s = %.2f", setting.name.c_str(), setting.defaultValue);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) {
                        try { workspace_.removeFloatSetting(currentBehavior, setting.name); reloadRegistry(); }
                        catch (const std::exception& e) { notify(e.what()); }
                        ImGui::PopID(); break;
                    }
                    ImGui::PopID();
                }
            }
        }
        if (ImGui::BeginPopupModal("Add float setting", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Property name", newSettingName_, sizeof(newSettingName_));
            ImGui::InputFloat("Default", &newSetting_.defaultValue);
            ImGui::InputFloat("Min", &newSetting_.minimum);
            ImGui::InputFloat("Max", &newSetting_.maximum);
            ImGui::InputFloat("Step", &newSetting_.step);
            if (ImGui::Button("Add property")) {
                try {
                    newSetting_.name = newSettingName_;
                    workspace_.addFloatSetting(currentBehavior, newSetting_);
                    reloadRegistry();
                    notify("Added property: " + newSetting_.name);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) { notify(e.what()); }
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (selectedCodeFile_.empty()) {
            ImGui::TextWrapped("Create a C++ behavior or select a file to edit. Game code recompiles when you press Build or Play. The editor never recompiles.");
        } else {
            ImGui::Text("%s%s", selectedCodeFile_.c_str(), sourceDirty_ ? " *unsaved" : "");
            const float height = std::max(50.f, ImGui::GetContentRegionAvail().y);
            if (ImGui::InputTextMultiline("##source", sourceBuffer_.data(), sourceBuffer_.size(),
                                          ImVec2(-FLT_MIN,height), ImGuiInputTextFlags_AllowTabInput)) sourceDirty_ = true;
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    }

    void buildOutputPanel() {
        ImGui::TextUnformatted(builder_.busy() ? "Building C++ game..." : "Build log");
        ImGui::SameLine();
        if (ImGui::Button("Build game")) requestBuild();
        ImGui::Separator();
        std::ifstream in(builder_.logPath(), std::ios::binary);
        if (!in) { ImGui::TextDisabled("No build has been started for this editor session."); return; }
        std::string log((std::istreambuf_iterator<char>(in)), {});
        if (log.size() > 160000) log.erase(0, log.size()-160000);
        ImGui::BeginChild("BuildScroll", ImVec2(0.f,0.f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(log.c_str());
        ImGui::EndChild();
    }

    void sceneViewport() {
        if (ImGui::Button("Play / Compile")) requestBuild(AfterBuild::Play);
        ImGui::SameLine();
        if (ImGui::Button("Stop Game")) stopGame();
        ImGui::SameLine();
        if (ImGui::Button("Center")) camera_ = {scene_.width/2.f,scene_.height/2.f};
        ImGui::SameLine();
        if (ImGui::Button("1:1")) zoom_ = 1.f;
        ImGui::SameLine();
        ImGui::TextDisabled("Zoom: %.0f%%   Middle-drag: pan", 100.f/zoom_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.f);
        ImGui::InputTextWithHint("##exportname", "EXE name", exportName_, sizeof(exportName_));
        ImGui::SameLine();
        if (ImGui::Button("Export EXE")) exportGame();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const unsigned rw = static_cast<unsigned>(std::max(1.f, available.x));
        const unsigned rh = static_cast<unsigned>(std::max(1.f, available.y));
        if (rw < 2 || rh < 2) return;
        if (viewport_.getSize() != sf::Vector2u{rw,rh}) viewport_.resize({rw,rh});
        viewportW_ = static_cast<float>(rw);
        viewportH_ = static_cast<float>(rh);
        const sf::View view(camera_, {viewportW_*zoom_,viewportH_*zoom_});
        viewport_.setView(view);
        const Scene& active = scene_;
        viewport_.clear(active.background);
        drawScene(viewport_, active, projectDir_, textures_, selectedId_, grid_, zoom_);
        viewport_.display();
        ImGui::Image(viewport_, {viewportW_,viewportH_}); // RenderTexture overload fixes vertical flipping.
        const bool hovered = ImGui::IsItemHovered();
        const auto start = ImGui::GetItemRectMin();
        auto& io = ImGui::GetIO();
        auto worldAt = [&](const ImVec2 mouse) -> sf::Vector2f {
            return {camera_.x + (mouse.x-start.x-viewportW_/2.f)*zoom_,
                    camera_.y + (mouse.y-start.y-viewportH_/2.f)*zoom_};
        };
        if (hovered) {
            if (io.MouseWheel != 0.f) {
                const sf::Vector2f before = worldAt(io.MousePos);
                zoom_ = std::clamp(zoom_*std::pow(0.85f, io.MouseWheel),0.2f,6.f);
                const sf::Vector2f after = worldAt(io.MousePos);
                camera_ += before-after;
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                camera_.x -= io.MouseDelta.x * zoom_;
                camera_.y -= io.MouseDelta.y * zoom_;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const sf::Vector2f at = worldAt(io.MousePos);
            selectedId_ = findObjectAt(scene_, at, projectDir_, textures_);
            draggingObject_ = selectedId_ >= 0;
            if (auto* o = scene_.find(selectedId_)) objectDragOffset_ = o->position-at;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) draggingObject_ = false;
        if (draggingObject_ && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            if (auto* o = scene_.find(selectedId_)) o->position = worldAt(io.MousePos)+objectDragOffset_;
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("SPRITE_ASSET")) {
                if (payload->Data && payload->DataSize > 1)
                    addSprite(static_cast<const char*>(payload->Data), worldAt(io.MousePos));
            }
            ImGui::EndDragDropTarget();
        }
    }
};
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        EditorApp app;
        app.run();
        if (SUCCEEDED(hr)) CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "SpriteForge error", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(hr)) CoUninitialize();
        return 1;
    }
}
