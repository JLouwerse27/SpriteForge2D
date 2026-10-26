#include "EngineCore.h"
#include "BitmapFont.h"
#include "Behaviors.h"
#include "ProjectWorkspace.h"
#include "BuildRunner.h"
#include "SteamProject.h"
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
#include <sstream>
#include <cctype>

using namespace forge;

namespace {
constexpr int kTilemapSelection = -2;

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

// Copy the full assets directory: C++ behaviors and tilemaps can reference
// images that are not present as ordinary scene objects.
void copyProjectAssets(const Scene& scene, const fs::path& oldRoot, const fs::path& newRoot) {
    const fs::path srcRoot = fs::absolute(oldRoot / "assets").lexically_normal();
    const fs::path dstRoot = fs::absolute(newRoot / "assets").lexically_normal();
    auto validateRel = [&](const std::string& relPath) {
        const fs::path rel = fs::path(relPath).lexically_normal();
        if (rel.empty() || rel.is_absolute() || *rel.begin() != fs::path("assets") || relPath.find("..") != std::string::npos)
            throw std::runtime_error("Invalid relative asset path: " + relPath);
        if (!fs::is_regular_file(oldRoot / rel))
            throw std::runtime_error("Missing asset: " + (oldRoot / rel).string());
    };
    for (const auto& object : scene.objects) validateRel(object.texture);
    if (scene.tilemap.enabled) {
        for (const auto& tile : scene.tilemap.tiles) {
            if (tile.id == 0 || tile.texture.empty()) continue;
            validateRel(tile.texture);
        }
    }
    if (!fs::exists(srcRoot)) return;
    if (srcRoot == dstRoot) return;
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

enum class UiGlyph { Play, Stop, Compile, Center, Paint, Rect, Pick, Erase, Add, Folder };

// Draw icons as ImGui vector primitives; no external icon font is necessary.
// The invisible leading spaces are part of the button label, not a second widget.
bool glyphButton(const char* label, UiGlyph glyph, ImVec2 size = {0.f, 0.f}, bool active = false) {
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.08f, .36f, .68f, 1.f));
    const std::string visible = std::string("      ") + label;
    const bool clicked = ImGui::Button(visible.c_str(), size);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c(a.x + 14.f, (a.y + b.y) * .5f);
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = glyph == UiGlyph::Play ? IM_COL32(64,222,130,255)
                      : glyph == UiGlyph::Stop ? IM_COL32(250,105,110,255)
                      : IM_COL32(225,238,255,255);
    switch (glyph) {
    case UiGlyph::Play: draw->AddTriangleFilled({c.x-4,c.y-6},{c.x+6,c.y},{c.x-4,c.y+6},color); break;
    case UiGlyph::Stop: draw->AddRectFilled({c.x-5,c.y-5},{c.x+5,c.y+5},color); break;
    case UiGlyph::Compile:
        draw->AddCircle(c,5.f,color,0,2.f);
        for(int i=0;i<8;++i){const float ang=i*3.14159265f/4.f;draw->AddLine({c.x+7.f*std::cos(ang),c.y+7.f*std::sin(ang)}, {c.x+9.f*std::cos(ang),c.y+9.f*std::sin(ang)},color,2.f);}
        break;
    case UiGlyph::Center: draw->AddCircle(c,5.f,color,0,1.5f);draw->AddLine({c.x-8,c.y},{c.x+8,c.y},color);draw->AddLine({c.x,c.y-8},{c.x,c.y+8},color); break;
    case UiGlyph::Paint: draw->AddLine({c.x-5,c.y+5},{c.x+4,c.y-4},color,3.f); draw->AddCircleFilled({c.x+4,c.y-4},2.f,color); break;
    case UiGlyph::Rect: draw->AddRect({c.x-6,c.y-5},{c.x+6,c.y+5},color,0.f,0,2.f); break;
    case UiGlyph::Pick: draw->AddLine({c.x-5,c.y+5},{c.x+6,c.y-6},color,2.f);draw->AddCircle({c.x+4,c.y-4},3.f,color,0,1.5f); break;
    case UiGlyph::Erase: draw->AddRectFilled({c.x-5,c.y-4},{c.x+4,c.y+4},color);draw->AddLine({c.x-7,c.y+6},{c.x+6,c.y+6},color,1.5f);break;
    case UiGlyph::Add: draw->AddLine({c.x-5,c.y},{c.x+5,c.y},color,2.f);draw->AddLine({c.x,c.y-5},{c.x,c.y+5},color,2.f);break;
    case UiGlyph::Folder: draw->AddRect({c.x-7,c.y-4},{c.x+7,c.y+5},color);draw->AddLine({c.x-7,c.y-5},{c.x-1,c.y-5},color);break;
    }
    if(active) ImGui::PopStyleColor();
    return clicked;
}

class EditorApp {
    sf::RenderWindow window_;
    sf::RenderTexture viewport_{{800u, 450u}};
    TextureCache textures_;
    BehaviorRegistry registry_;
    ProjectWorkspace workspace_;
    SteamProject steam_;
    char steamSdkPath_[1024] = {};
    int steamAppIDInput_ = 0;
    char newAchievementID_[128] = {};
    char newAchievementName_[256] = {};
    char newAchievementDescription_[512] = {};
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
    int selectedTileId_ = 0;
    bool tilePaintMode_ = false;
    bool tileEraser_ = false;
    sf::Vector2i hoveredTile_{-1, -1};
    enum class TileTool { Paint, Erase, Rectangle, Eyedropper };
    TileTool tileTool_ = TileTool::Paint;
    int brushSize_ = 1;
    int tileLayer_ = 0;
    sf::Vector2i rectangleStart_{-1, -1};
    bool showColliders_ = false;
    BitmapFont font_{};
    int selectedGlyph_ = 65;
    char fontFileName_[96] = "CustomPixelFont";
    char fontName_[96] = "Custom Pixel Font";
    bool fontDirty_ = false;
    int newTextCounter_ = 0;
    sf::Vector2f inspectorForce_{0.f,0.f};
    float inspectorTorque_ = 0.f;
    bool fontEditorVisible_ = true;
    bool fontGlyphWindowOpen_ = false;
    bool assetsExpanded_ = true;
    int assetCategory_ = 0; // 0 sprites, 1 bitmap fonts
    bool assetsGridView_ = true;
    bool arrayFollowCursor_ = true;
    int arrayFirstX_ = 0;
    int arrayFirstY_ = 0;
    int gridSizePreview_ = 16;

public:
    EditorApp()
        : window_(sf::VideoMode({1440u, 860u}), "SpriteForge 2D - SFML Scene Editor"),
          projectDir_(executableDirectory() / "Project"),
          currentSceneFile_(projectDir_ / "scene.json") {
        workspace_.setRoot(projectDir_);
        workspace_.initialize();
        loadSteamSettings();
        reloadRegistry();
        window_.setVerticalSyncEnabled(false);
        window_.setFramerateLimit(480);
        font_.reset(8, 8);
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
        // Blue-black theme resembling the SpriteForge tilemap concept.
        auto& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = ImVec4(.035f,.065f,.11f,1.f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(.045f,.09f,.15f,1.f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(.07f,.15f,.24f,1.f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(.075f,.13f,.22f,1.f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.12f,.21f,.33f,1.f);
        style.Colors[ImGuiCol_Button] = ImVec4(.09f,.21f,.36f,1.f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.16f,.32f,.51f,1.f);
        style.Colors[ImGuiCol_Header] = ImVec4(.11f,.27f,.45f,1.f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(.17f,.34f,.55f,1.f);
        style.Colors[ImGuiCol_Tab] = ImVec4(.06f,.14f,.24f,1.f);
        style.Colors[ImGuiCol_TabSelected] = ImVec4(.10f,.29f,.49f,1.f);
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
            if (fontGlyphWindowOpen_) {
                ImGui::SetNextWindowSize(ImVec2(530.f, 670.f), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Bitmap Font - Glyph Editor", &fontGlyphWindowOpen_)) fontPanel();
                ImGui::End();
            }
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
    void loadSteamSettings() {
        steam_ = SteamProject::load(projectDir_);
        std::memset(steamSdkPath_, 0, sizeof(steamSdkPath_));
        const std::string sdk = steam_.sdkPath.string();
        std::strncpy(steamSdkPath_, sdk.c_str(), sizeof(steamSdkPath_) - 1);
        steamAppIDInput_ = static_cast<int>(steam_.appId);
    }

    bool saveSteamSettings() {
        try {
            if (steamAppIDInput_ < 0) throw std::runtime_error("Steam App ID must be positive");
            steam_.appId = static_cast<std::uint32_t>(steamAppIDInput_);
            steam_.sdkPath = steamSdkPath_;
            steam_.save(projectDir_);
            return true;
        } catch (const std::exception& e) { notify(std::string("Steam settings error: ") + e.what()); return false; }
    }

    void steamPanel() {
        ImGui::TextUnformatted("Steamworks (optional per game project)");
        ImGui::Separator();
        ImGui::Checkbox("Enable Steamworks for this game", &steam_.enabled);
        ImGui::InputInt("Steam App ID", &steamAppIDInput_);
        ImGui::InputText("Steamworks SDK folder", steamSdkPath_, sizeof(steamSdkPath_));
        if (ImGui::Button("Browse SDK...")) {
            const fs::path folder = chooseFolder(window_.getNativeHandle(), L"Choose the Steamworks SDK root folder (contains public and redistributable_bin)");
            if (!folder.empty()) {
                const std::string dir = folder.string();
                std::strncpy(steamSdkPath_, dir.c_str(), sizeof(steamSdkPath_)-1);
                steamSdkPath_[sizeof(steamSdkPath_)-1] = 0;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Steam settings")) {
            if (saveSteamSettings()) notify("Saved project Steam settings. Build to apply.");
        }
        ImGui::TextWrapped("Download your official SDK at partner.steamgames.com/doc/sdk. It is NOT included."
                           " Steam overlay and achievements require Steam to be running and a valid App ID.");
        ImGui::TextWrapped("The IDs below are LOCAL definitions. Create and publish the same API names in Steamworks Partner > Achievements.");
        ImGui::Separator();
        ImGui::TextUnformatted("Achievement definitions");
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(steam_.achievements.size()); ++i) {
            const auto& a = steam_.achievements[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::Text("%s  |  %s", a.apiName.c_str(), a.displayName.c_str());
            if (!a.description.empty()) ImGui::TextWrapped("%s", a.description.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeIndex = i;
            ImGui::PopID();
        }
        if (removeIndex >= 0) {
            steam_.achievements.erase(steam_.achievements.begin()+removeIndex);
            saveSteamSettings();
        }
        ImGui::Separator();
        ImGui::InputTextWithHint("##steamachid", "ACH_FIRST_WIN (API name)", newAchievementID_, sizeof(newAchievementID_));
        ImGui::InputTextWithHint("##steamachname", "First Win (display name)", newAchievementName_, sizeof(newAchievementName_));
        ImGui::InputTextWithHint("##steamachdesc", "Description", newAchievementDescription_, sizeof(newAchievementDescription_));
        if (ImGui::Button("Add achievement")) {
            steam_.achievements.push_back({newAchievementID_, newAchievementName_, newAchievementDescription_});
            if (saveSteamSettings()) {
                newAchievementID_[0] = newAchievementName_[0] = newAchievementDescription_[0] = '\0';
                notify("Achievement added locally; also create it in Steamworks Partner.");
            } else steam_.achievements.pop_back();
        }
        ImGui::Separator();
        ImGui::TextWrapped("From game behavior C++: #include \"SteamIntegration.h\" then"
                           " forge::steam::unlockAchievement(\"ACH_FIRST_WIN\");");
        ImGui::TextWrapped("Multiplayer foundation: createLobby(), showInviteDialog(), send(), receive()."
                           " You must implement object synchronization and gameplay authority in C++.");
        ImGui::TextWrapped("Steam-ready Export copies steam_api64.dll but never packages steam_appid.txt."
                           " Upload/release via Steamworks SteamPipe separately.");
    }

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
            loadSteamSettings();
            reloadRegistry();
            selectedCodeFile_.clear(); sourceDirty_ = false;
            selectedId_ = inspectorId_ = -1;
            selectedTileId_ = 0;
            tilePaintMode_ = false;
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
            loadSteamSettings();
            reloadRegistry();
            selectedCodeFile_.clear(); sourceDirty_ = false;
            selectedId_ = inspectorId_ = -1;
            selectedTileId_ = 0;
            tilePaintMode_ = false;
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
        // Steam development App ID is beside GameRuntime.exe, not the project root.
        const std::wstring cwd = exe.parent_path().wstring();
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
        if (!saveCode() || !saveSteamSettings()) return;
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
            builder_.start(projectDir_, sdk, steam_);
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
        const fs::path steamDll = runtime.parent_path() / "steam_api64.dll";
        if (steam_.enabled) {
            if (!fs::is_regular_file(steamDll)) throw std::runtime_error("Steam DLL missing after build");
            fs::copy_file(steamDll, out / "steam_api64.dll", fs::copy_options::overwrite_existing);
        } else {
            std::error_code ec;
            fs::remove(out / "steam_api64.dll", ec); // remove stale exported dependency
        }
        // A development steam_appid.txt MUST NOT be included in a Steam shipping build.
        { std::error_code ec; fs::remove(out / "steam_appid.txt", ec); }
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

    fs::path currentFontFile() const { return projectDir_ / "assets" / "fonts" / (std::string(fontFileName_) + ".sffont.json"); }
    bool saveFont() {
        std::string err;
        font_.name=fontName_;
        for(const char* c=fontFileName_;*c;++c) if(!std::isalnum(static_cast<unsigned char>(*c)) && *c!='_' && *c!='-') {
            notify("Font filename supports only letters, digits, _ and -"); return false;
        }
        if(!saveBitmapFont(font_,currentFontFile(),err)) { notify("Font save: "+err); return false; }
        fontDirty_=false;
        clearLoadedBitmapFonts();
        notify("Saved font " + currentFontFile().string());
        return true;
    }
    void fontPanel() {
        if(!fontEditorVisible_) { if(ImGui::Button("Open Font Editor")) fontEditorVisible_=true; return; }
        if(ImGui::Button("New")) {
            font_.reset(8,8);fontDirty_=true;std::strncpy(fontFileName_,"CustomPixelFont",sizeof(fontFileName_)-1);
        }
        ImGui::SameLine();if(ImGui::Button("Save font")) saveFont();
        ImGui::SameLine();if(ImGui::Button("Export atlas")) {
            if(saveFont()) {
                std::string err;
                const fs::path png=currentFontFile().parent_path() / (std::string(fontFileName_)+".png");
                if(exportBitmapAtlas(font_,png,err)) notify("Exported "+png.string());
                else notify("Font PNG: "+err);
            }
        }
        ImGui::SetNextItemWidth(160.f);ImGui::InputText("File name",fontFileName_,sizeof(fontFileName_));
        ImGui::SetNextItemWidth(160.f);ImGui::InputText("Font name",fontName_,sizeof(fontName_));
        if(ImGui::Button("Import font...")) {
            const fs::path file=chooseFile(false,window_.getNativeHandle());
            if(!file.empty()) {
                BitmapFont candidate;
                std::string err;
                if(!loadBitmapFont(candidate,file,err))notify("Import font: "+err);
                else {
                    std::string stem=file.filename().string();
                    const std::string suffix=".sffont.json";
                    if(stem.size()>=suffix.size() && stem.substr(stem.size()-suffix.size())==suffix)
                        stem.erase(stem.size()-suffix.size());
                    else stem=file.stem().string();
                    stem=safeGameName(stem.c_str());
                    std::strncpy(fontFileName_,stem.c_str(),sizeof(fontFileName_)-1);
                    fontFileName_[sizeof(fontFileName_)-1]=0;
                    font_=std::move(candidate);
                    std::strncpy(fontName_,font_.name.c_str(),sizeof(fontName_)-1);
                    fontName_[sizeof(fontName_)-1]=0;
                    fontDirty_=true;
                    saveFont();
                }
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Load font")) {
            std::string err;
            if(loadBitmapFont(font_,currentFontFile(),err)) {
                std::strncpy(fontName_,font_.name.c_str(),sizeof(fontName_)-1);
                fontName_[sizeof(fontName_)-1]=0; fontDirty_=false;
                clearLoadedBitmapFonts();
                notify("Loaded font");
            } else notify("Font load: "+err);
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(115.f);
        if(ImGui::InputInt("Glyph ASCII",&selectedGlyph_)) selectedGlyph_=std::clamp(selectedGlyph_,32,126);
        ImGui::SameLine();ImGui::Text("%c",selectedGlyph_);
        int size[2]={font_.width,font_.height};
        if(ImGui::InputInt2("Glyph W/H",size)) {
            if(size[0]!=font_.width||size[1]!=font_.height) { font_.reset(size[0],size[1]);fontDirty_=true; }
        }
        if(ImGui::InputInt("Advance",&font_.advance)) {font_.advance=std::clamp(font_.advance,1,64);fontDirty_=true;}
        if(ImGui::InputInt("Baseline",&font_.baseline)) {font_.baseline=std::clamp(font_.baseline,0,font_.height);fontDirty_=true;}
        const float cell=std::clamp(195.f/static_cast<float>(std::max(font_.width,font_.height)),5.f,22.f);
        ImDrawList* dl=ImGui::GetWindowDrawList();
        auto& pixels=font_.glyph(selectedGlyph_);
        for(int y=0;y<font_.height;++y) {
            for(int x=0;x<font_.width;++x) {
                ImGui::PushID(y*font_.width+x);
                const ImVec2 top=ImGui::GetCursorScreenPos();
                const bool fill=pixels[static_cast<size_t>(y*font_.width+x)]!=0;
                dl->AddRectFilled(top,ImVec2(top.x+cell-1,top.y+cell-1),fill?IM_COL32(240,245,255,255):IM_COL32(35,44,62,255));
                if(ImGui::InvisibleButton("##px",ImVec2(cell,cell))) {pixels[static_cast<size_t>(y*font_.width+x)]=fill?0:1; fontDirty_=true;}
                if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    pixels[static_cast<size_t>(y*font_.width+x)]=1;fontDirty_=true;
                }
                if(ImGui::IsItemHovered()&&ImGui::IsMouseDown(ImGuiMouseButton_Right)) {pixels[static_cast<size_t>(y*font_.width+x)]=0;fontDirty_=true;}
                if(x+1<font_.width)ImGui::SameLine(0.f,0.f);
                ImGui::PopID();
            }
        }
        ImGui::TextDisabled("Left: paint  Right: erase   %s",fontDirty_?"*unsaved":"saved");
        ImGui::TextDisabled("ASCII 32-126; usable as scene text after saving.");
        for(int i=32;i<=126;++i) {
            ImGui::PushID(i);
            const std::string str(1,static_cast<char>(i));
            if(ImGui::Button(str.c_str(),ImVec2(22.f,22.f))) selectedGlyph_=i;
            if((i-31)%9!=0 && i!=126)ImGui::SameLine(0.f,2.f);
            ImGui::PopID();
        }
    }

    void addTextObject() {
        TextObject obj;
        obj.id=scene_.nextId++; obj.name="Bitmap Text";
        obj.text="HELLO WORLD";obj.position=camera_;
        if(fs::exists(currentFontFile()))
            obj.bitmapFont="assets/fonts/"+std::string(fontFileName_)+".sffont.json";
        scene_.textObjects.push_back(std::move(obj));
        selectedId_=scene_.textObjects.back().id;
        notify("Created text object; use Inspector to assign a bitmap font.");
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
            if (ImGui::BeginMenu("Tools")) {
                if(ImGui::MenuItem("Create bitmap text")) addTextObject();
                if(ImGui::MenuItem("Save bitmap font")) saveFont();
                ImGui::MenuItem("Font editor",nullptr,&fontEditorVisible_);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Grid", nullptr, &grid_);
                ImGui::MenuItem("Colliders", nullptr, &showColliders_);
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

    void hierarchyPanel() {
        if(ImGui::Button("+ Sprite")) {
            const auto all=assetsIn(projectDir_);
            if(all.empty())notify("Import an image first.");
            else addSprite("assets/"+all.front().filename().string(),camera_);
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("+ Tilemap")) {
            ensureTilemapEnabled();selectedId_=kTilemapSelection;tilePaintMode_=true;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("+ Text"))addTextObject();
        if(ImGui::Selectable("MainScene",selectedId_==-1))selectedId_=-1;
        if(scene_.tilemap.enabled && ImGui::Selectable(scene_.tilemap.name.c_str(),selectedId_==kTilemapSelection))
            selectedId_=kTilemapSelection;
        for(const auto& obj:scene_.objects) {
            ImGui::PushID(obj.id);
            if(ImGui::Selectable(obj.name.c_str(),selectedId_==obj.id))selectedId_=obj.id;
            ImGui::PopID();
        }
        for(const auto& t:scene_.textObjects) {
            ImGui::PushID(t.id);
            if(ImGui::Selectable(t.name.c_str(),selectedId_==t.id))selectedId_=t.id;
            ImGui::PopID();
        }
        if(ImGui::Button("Delete selected")) {
            if(selectedId_==kTilemapSelection){scene_.tilemap=TilemapData{};tilePaintMode_=false;}
            else if(scene_.find(selectedId_))scene_.remove(selectedId_);
            else scene_.textObjects.erase(std::remove_if(scene_.textObjects.begin(),scene_.textObjects.end(),
                [&](const TextObject& t){return t.id==selectedId_;}),scene_.textObjects.end());
            selectedId_=-1;
        }
    }

    void arrayDataPanel() {
        if (ImGui::BeginTabBar("WorldTabs")) {
            if (ImGui::BeginTabItem("Array View")) {
                if (!scene_.tilemap.enabled) { ImGui::TextDisabled("Create a tilemap to inspect its cells."); }
                else {
                    const auto& tm = scene_.tilemap;
                    if (arrayFollowCursor_ && tm.inBounds(hoveredTile_.x, hoveredTile_.y)) {
                        arrayFirstX_ = std::clamp(hoveredTile_.x - 4, 0, std::max(0,tm.width - 1));
                        arrayFirstY_ = std::clamp(hoveredTile_.y - 4, 0, std::max(0,tm.height - 1));
                    }
                    if (ImGui::BeginChild("##ArrayCells",ImVec2(ImGui::GetContentRegionAvail().x * .63f, 0.f),true,ImGuiWindowFlags_HorizontalScrollbar)) {
                        const int ncols = std::min(10, tm.width-arrayFirstX_);
                        const int nrows = std::min(12, tm.height-arrayFirstY_);
                        const auto flags=ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_BordersInnerH|
                                         ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit|
                                         ImGuiTableFlags_ScrollY;
                        if (ImGui::BeginTable("TileArray",ncols+1,flags,ImVec2(0.f,0.f))) {
                            ImGui::TableSetupColumn("Y/X",ImGuiTableColumnFlags_WidthFixed,38.f);
                            for (int x=0; x<ncols; ++x) {
                                const std::string col=std::to_string(arrayFirstX_+x);
                                ImGui::TableSetupColumn(col.c_str(),ImGuiTableColumnFlags_WidthFixed,27.f);
                            }
                            ImGui::TableSetupScrollFreeze(1,1);
                            ImGui::TableHeadersRow();
                            for(int y=0;y<nrows;++y) {
                                const int row=arrayFirstY_+y;
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);ImGui::Text("%d",row);
                                for(int x=0;x<ncols;++x) {
                                    const int col=arrayFirstX_+x;
                                    const int value=tm.getLayer(tileLayer_,col,row);
                                    ImGui::TableSetColumnIndex(x+1);
                                    ImGui::PushID(row*tm.width+col);
                                    const bool selected=hoveredTile_.x==col&&hoveredTile_.y==row;
                                    if(selected)ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,IM_COL32(24,94,157,150));
                                    const std::string label=std::to_string(value);
                                    if(ImGui::Selectable(label.c_str(),selected,ImGuiSelectableFlags_None,ImVec2(-FLT_MIN,0))) {
                                        selectedId_=kTilemapSelection;tilePaintMode_=true;
                                        scene_.tilemap.setLayer(tileLayer_,col,row,selectedTileId_);
                                    }
                                    if(ImGui::IsItemClicked(ImGuiMouseButton_Right))scene_.tilemap.setLayer(tileLayer_,col,row,0);
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndChild();ImGui::SameLine();
                    ImGui::BeginChild("ArrayStats",ImVec2(0,0),true);
                    ImGui::Text("Layer: %d",tileLayer_);
                    ImGui::Text("Cursor: %d, %d",hoveredTile_.x,hoveredTile_.y);
                    if(tm.inBounds(hoveredTile_.x,hoveredTile_.y)) {
                        const int val=tm.getLayer(tileLayer_,hoveredTile_.x,hoveredTile_.y);
                        const auto* def=tm.tileById(val);
                        ImGui::Text("Block: %d (%s)",val,def?def->name.c_str():"Air");
                        ImGui::Text("Index: [%d][%d]",hoveredTile_.y,hoveredTile_.x);
                        ImGui::Text("Chunk: (%d, %d)", hoveredTile_.x/std::max(1,tm.chunkSize),hoveredTile_.y/std::max(1,tm.chunkSize));
                    }
                    ImGui::Text("World: %d x %d",tm.width,tm.height);
                    ImGui::Text("Tile size: %d px",tm.blockSize);
                    ImGui::Text("Cells: %zu",tm.cells.size());
                    ImGui::Checkbox("Follow cursor",&arrayFollowCursor_);
                    if(!arrayFollowCursor_) {
                        ImGui::SetNextItemWidth(85.f);ImGui::InputInt("From X",&arrayFirstX_);
                        ImGui::SetNextItemWidth(85.f);ImGui::InputInt("From Y",&arrayFirstY_);
                        arrayFirstX_=std::clamp(arrayFirstX_,0,std::max(0,tm.width-1));
                        arrayFirstY_=std::clamp(arrayFirstY_,0,std::max(0,tm.height-1));
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Block Info")) {
                if(scene_.tilemap.enabled) {
                    const auto& tm=scene_.tilemap;
                    ImGui::Text("Layer %d, selected tile ID %d",tileLayer_,selectedTileId_);
                    if(const auto* def=tm.tileById(selectedTileId_)) {
                        ImGui::Text("Name: %s",def->name.c_str());
                        ImGui::Text("Texture: %s",def->texture.c_str());
                        ImGui::Text("Solid: %s",def->solid?"yes":"no");
                    }
                    if(tm.inBounds(hoveredTile_.x,hoveredTile_.y)) {
                        ImGui::Text("Hovered cell: [%d][%d]",hoveredTile_.y,hoveredTile_.x);
                    }
                } else ImGui::TextDisabled("No tilemap selected.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Chunk Data")) {
                if(scene_.tilemap.enabled) {
                    const auto& tm=scene_.tilemap;
                    const int cw=(tm.width+tm.chunkSize-1)/tm.chunkSize,ch=(tm.height+tm.chunkSize-1)/tm.chunkSize;
                    ImGui::Text("Logical regions: %d x %d (%d total)",cw,ch,cw*ch);
                    ImGui::Text("Chunk dimensions: %d x %d cells",tm.chunkSize,tm.chunkSize);
                    ImGui::TextWrapped("Rendering batches visible tiles; disk chunk streaming is not yet implemented.");
                }else ImGui::TextDisabled("No tilemap selected.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Code")) {codePanel();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Build Output")) {buildOutputPanel();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Steam")) {steamPanel();ImGui::EndTabItem();}
            ImGui::EndTabBar();
        }
    }

    void tilePalettePanel() {
        if(!scene_.tilemap.enabled) {
            ImGui::TextWrapped("Create a tilemap to paint a block array.");
            if(glyphButton("Create Tilemap",UiGlyph::Add)) {ensureTilemapEnabled();selectedId_=kTilemapSelection;tilePaintMode_=true;}
            return;
        }
        if (ImGui::BeginTabBar("PaletteTabs")) {
            if (ImGui::BeginTabItem("Tiles")) {
                const auto all=assetsIn(projectDir_);
                const float tileSide=std::clamp((ImGui::GetContentRegionAvail().x-30.f)/7.f,27.f,48.f);
                const int perRow=std::max(1,(int)(ImGui::GetContentRegionAvail().x/(tileSide+6.f)));
                if(ImGui::Button("Air##tile",{tileSide,tileSide})) {selectedTileId_=0;tileTool_=TileTool::Erase;tilePaintMode_=true;}
                if (ImGui::IsItemHovered())ImGui::SetTooltip("Erase: ID 0");
                ImGui::SameLine();
                int n=1;
                for(const auto& path:all) {
                    const auto* tex=textures_.get(path);if(!tex)continue;
                    const int id=ensureTileDefinition("assets/"+path.filename().string());
                    ImGui::PushID(id);
                    if(ImGui::ImageButton("##Tile",*tex,{tileSide,tileSide})) {
                        selectedTileId_=id;tileTool_=TileTool::Paint;tilePaintMode_=true;
                    }
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("%d: %s",id,path.stem().string().c_str());
                    if(++n%perRow!=0)ImGui::SameLine(0.f,4.f);
                    ImGui::PopID();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Tools")) {
                ImGui::SliderInt("Brush size",&brushSize_,1,16);
                ImGui::Checkbox("Paint enabled",&tilePaintMode_);
                ImGui::TextWrapped("Shift+left-drag or middle-drag pans. Right-click erases.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        const float gap=ImGui::GetStyle().ItemSpacing.x;
        const float width=std::max(58.f,(ImGui::GetContentRegionAvail().x-3.f*gap)/4.f);
        auto action=[&](const char* text,UiGlyph glyph,TileTool tool) {
            if(glyphButton(text,glyph,{width,34.f},tileTool_==tool)) {tileTool_=tool;tilePaintMode_=true;}
        };
        action("Paint",UiGlyph::Paint,TileTool::Paint);ImGui::SameLine();
        action("Fill",UiGlyph::Rect,TileTool::Rectangle);ImGui::SameLine();
        action("Pick",UiGlyph::Pick,TileTool::Eyedropper);ImGui::SameLine();
        action("Erase",UiGlyph::Erase,TileTool::Erase);
        ImGui::SetNextItemWidth(95.f);ImGui::Combo("Layer",&tileLayer_,"Layer 0\0Layer 1\0Layer 2\0");
        ImGui::SameLine();ImGui::SetNextItemWidth(95.f);ImGui::SliderInt("Brush",&brushSize_,1,16);
        ImGui::TextDisabled("Selected block: %d",selectedTileId_);
    }

    void toolbar() {
        if(glyphButton("Play",UiGlyph::Play,{95.f,27.f}))requestBuild(AfterBuild::Play);
        ImGui::SameLine();
        if(glyphButton("Compile",UiGlyph::Compile,{115.f,27.f}))requestBuild();
        ImGui::SameLine();
        if(glyphButton("Stop Game",UiGlyph::Stop,{123.f,27.f}))stopGame();
        ImGui::SameLine();
        if(glyphButton("Center",UiGlyph::Center,{105.f,27.f}))camera_={scene_.width*.5f,scene_.height*.5f};
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95.f);
        const char* zoomNames[]={"50%","75%","100%","150%","200%","300%","400%","600%"};
        const float zoomValues[]={2.f,1.3333333f,1.f,.6666667f,.5f,.3333333f,.25f,.1666667f};
        int chosen=2;
        float best=999.f;
        for(int i=0;i<8;++i){const float diff=std::abs(zoom_-zoomValues[i]);if(diff<best){best=diff;chosen=i;}}
        if(ImGui::Combo("Zoom",&chosen,zoomNames,8))zoom_=zoomValues[chosen];
        ImGui::SameLine();
        ImGui::SetNextItemWidth(83.f);
        const int options[]={8,16,32,64,128};
        int chosenGrid=1;
        for(int i=0;i<5;++i)if(options[i]==gridSizePreview_)chosenGrid=i;
        const char* gridNames[]={"8 px","16 px","32 px","64 px","128 px"};
        if(ImGui::Combo("Grid",&chosenGrid,gridNames,5))gridSizePreview_=options[chosenGrid];
        ImGui::SameLine();ImGui::Checkbox("Show Grid",&grid_);
        ImGui::SameLine();ImGui::Checkbox("Show Colliders",&showColliders_);
        ImGui::SameLine();
        ImGui::TextDisabled("Pan: MMB / Shift+LMB");
        ImGui::SameLine();
        if(glyphButton("Export",UiGlyph::Folder,{86.f,27.f}))exportGame();
    }

    void assetsPanel() {
        ImGui::TextUnformatted("Assets");
        const float right=ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(right-67.f);
        if(ImGui::SmallButton(assetsExpanded_?"v##assetcollapse":">##assetcollapse"))assetsExpanded_=!assetsExpanded_;
        ImGui::SameLine(right-38.f);
        if(ImGui::SmallButton("+##assetcreate"))ImGui::OpenPopup("NewAssetMenu");
        if(ImGui::BeginPopup("NewAssetMenu")){
            if(ImGui::MenuItem("Import PNG / JPG..."))importImage();
            if(ImGui::MenuItem("New bitmap font...")){
                font_.reset(8,8);fontDirty_=true;fontGlyphWindowOpen_=true;fontEditorVisible_=true;
            }
            if(ImGui::MenuItem("Add sprite to scene")){
                const auto files=assetsIn(projectDir_);
                if(!files.empty())addSprite("assets/"+files.front().filename().string(),camera_);
                else notify("Import an image before adding a sprite.");
            }
            ImGui::EndPopup();
        }
        if(!assetsExpanded_) {
            ImGui::TextDisabled("Assets hidden. Click > to expand.");
            return;
        }
        const char* categories[]={"Assets / Sprites","Assets / Bitmap Fonts"};
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-66.f);
        ImGui::Combo("##AssetsCategory",&assetCategory_,categories,2);
        ImGui::SameLine();
        if(ImGui::SmallButton("Grid"))assetsGridView_=true;
        ImGui::SameLine();
        if(ImGui::SmallButton("List"))assetsGridView_=false;
        ImGui::Separator();
        if(assetCategory_==0)assetBrowser();
        else {
            const fs::path fontsDir=projectDir_/"assets"/"fonts";
            bool found=false;
            if(fs::exists(fontsDir))for(const auto& entry:fs::directory_iterator(fontsDir)) {
                if(!entry.is_regular_file() || entry.path().extension()!=".json")continue;
                found=true;
                const std::string filename=entry.path().filename().string();
                if(ImGui::Selectable(filename.c_str())) {
                    BitmapFont loaded;std::string err;
                    if(loadBitmapFont(loaded,entry.path(),err)) {
                        font_=std::move(loaded);
                        const std::string stem=filename.substr(0,filename.find(".sffont.json"));
                        std::strncpy(fontFileName_,stem.c_str(),sizeof(fontFileName_)-1);fontFileName_[sizeof(fontFileName_)-1]=0;
                        std::strncpy(fontName_,font_.name.c_str(),sizeof(fontName_)-1);fontName_[sizeof(fontName_)-1]=0;
                        fontDirty_=false;fontGlyphWindowOpen_=true;fontEditorVisible_=true;
                    } else notify(err);
                }
            }
            if(!found)ImGui::TextDisabled("No bitmap fonts. Use + > New bitmap font.");
        }
    }

    void physicsSidebar() {
        if(ImGui::CollapsingHeader("Physics",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Newtonian simulation");
            if(Object* obj=scene_.find(selectedId_)) {
                PhysicsBody& b=obj->physics;
                ImGui::Checkbox("Enable Physics",&b.enabled);
                const char* kinds[]={"Static","Dynamic","Kinematic"};int type=(int)b.type;
                if(ImGui::Combo("Body type",&type,kinds,3))b.type=(BodyType)type;
                ImGui::DragFloat("Mass",&b.mass,.1f,.001f,100000.f);
                ImGui::DragFloat2("Velocity",&b.velocity.x,1.f);
                ImGui::DragFloat2("Acceleration",&b.acceleration.x,1.f);
                ImGui::DragFloat("Linear Drag",&b.linearDrag,.02f,0.f,100.f);
                ImGui::DragFloat("Angular Drag",&b.angularDrag,.02f,0.f,100.f);
                ImGui::SliderFloat("Restitution",&b.restitution,0.f,1.f);
                ImGui::Checkbox("Tile collisions",&b.collideWithTiles);
                ImGui::Checkbox("Object collisions",&b.collideWithBodies);
            } else {
                ImGui::TextWrapped("World settings (select a sprite to edit its body)");
                ImGui::DragFloat2("Gravity",&scene_.physics.gravity.x,2.f);
                ImGui::InputInt("Fixed simulation Hz",&scene_.physics.fixedHz);
                scene_.physics.fixedHz=std::clamp(scene_.physics.fixedHz,30,480);
                ImGui::InputInt("Maximum FPS",&scene_.physics.maxRenderFps);
                scene_.physics.maxRenderFps=std::clamp(scene_.physics.maxRenderFps,30,1000);
                ImGui::TextDisabled("Default: 240 physics Hz / 480 FPS cap");
            }
        }
    }

    void compactFontPanel() {
        if(glyphButton("Create Font",UiGlyph::Add,{133.f,25.f})) {
            font_.reset(8,8);fontDirty_=true;
            std::strncpy(fontFileName_,"CustomPixelFont",sizeof(fontFileName_)-1);
            fontGlyphWindowOpen_=true;fontEditorVisible_=true;
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("Edit glyphs..."))fontGlyphWindowOpen_=true;
        ImGui::Separator();
        ImGui::TextDisabled("Current font: %s%s",fontName_,fontDirty_?" *":"");
        ImGui::BeginChild("##GlyphList",ImVec2(0.f,std::max(70.f,ImGui::GetContentRegionAvail().y*.56f)),true);
        const float spacing=ImGui::GetStyle().ItemSpacing.x;
        const int count=std::max(1,(int)(ImGui::GetContentRegionAvail().x/(24.f+spacing)));
        for(int ch=32;ch<127;++ch) {
            ImGui::PushID(ch);
            char label[8]={};label[0]=(char)ch;if(ch==32)std::strcpy(label,"Sp");
            if(ImGui::Button(label,{23.f,23.f})) {selectedGlyph_=ch;fontGlyphWindowOpen_=true;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Edit ASCII %d",ch);
            if((ch-31)%count!=0)ImGui::SameLine(0.f,spacing);
            ImGui::PopID();
        }
        ImGui::EndChild();
        const int size[2]={font_.width,font_.height};
        ImGui::Text("Character size: %d x %d",size[0],size[1]);
        ImGui::Text("Advance: %d  Baseline: %d",font_.advance,font_.baseline);
        if(ImGui::SmallButton("Save font"))saveFont();
        ImGui::SameLine();if(ImGui::SmallButton("Open full editor"))fontGlyphWindowOpen_=true;
    }

    void mainPanels() {
        const auto sz=ImGui::GetIO().DisplaySize;
        const float menuBottom=ImGui::GetFrameHeight()+2.f;
        const float toolbarH=39.f,top=menuBottom+toolbarH,bottom=25.f;
        const float left=std::clamp(sz.x*.205f,230.f,330.f);
        const float right=std::clamp(sz.x*.233f,265.f,380.f);
        const float mid=std::max(180.f,sz.x-left-right);
        const float contentH=std::max(300.f,sz.y-top-bottom);
        const float hierarchyH=std::clamp(contentH*.275f,150.f,255.f);
        const float assetsH=assetsExpanded_ ? std::clamp(contentH*.49f,190.f,450.f) : 62.f;
        const float physH=std::max(85.f,contentH-hierarchyH-assetsH);
        const float baseH=codeExpanded_ ? std::clamp(contentH*.62f,220.f,contentH-120.f) : std::clamp(contentH*.265f,170.f,280.f);
        const float sceneH=std::max(90.f,contentH-baseH);
        const float dataW=mid*.65f,fontW=mid*.35f;
        const float paletteH=std::clamp(contentH*.34f,210.f,330.f);
        const auto flags=ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos({0.f,menuBottom});ImGui::SetNextWindowSize({sz.x,toolbarH});
        ImGui::Begin("##SpriteForgeToolbar",nullptr,flags|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar);toolbar();ImGui::End();
        ImGui::SetNextWindowPos({0.f,top});ImGui::SetNextWindowSize({left,hierarchyH});
        ImGui::Begin("Hierarchy",nullptr,flags);hierarchyPanel();ImGui::End();
        ImGui::SetNextWindowPos({0.f,top+hierarchyH});ImGui::SetNextWindowSize({left,assetsH});
        ImGui::Begin("##SpriteForgeAssets",nullptr,flags|ImGuiWindowFlags_NoTitleBar);assetsPanel();ImGui::End();
        ImGui::SetNextWindowPos({0.f,top+hierarchyH+assetsH});ImGui::SetNextWindowSize({left,physH});
        ImGui::Begin("##SpriteForgePhysics",nullptr,flags|ImGuiWindowFlags_NoTitleBar);physicsSidebar();ImGui::End();
        ImGui::SetNextWindowPos({left,top});ImGui::SetNextWindowSize({mid,sceneH});
        ImGui::Begin("Scene View",nullptr,flags|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        sceneViewport();ImGui::End();
        ImGui::SetNextWindowPos({left,top+sceneH});ImGui::SetNextWindowSize({dataW,baseH});
        ImGui::Begin("Array / World Data",nullptr,flags);arrayDataPanel();ImGui::End();
        ImGui::SetNextWindowPos({left+dataW,top+sceneH});ImGui::SetNextWindowSize({fontW,baseH});
        ImGui::Begin("Font Editor",nullptr,flags);compactFontPanel();ImGui::End();
        ImGui::SetNextWindowPos({left+mid,top});ImGui::SetNextWindowSize({right,contentH-paletteH});
        ImGui::Begin("Inspector",nullptr,flags);inspector();ImGui::End();
        ImGui::SetNextWindowPos({left+mid,top+contentH-paletteH});ImGui::SetNextWindowSize({right,paletteH});
        ImGui::Begin("Tile Palette / Array Tools",nullptr,flags);tilePalettePanel();ImGui::End();
        ImGui::SetNextWindowPos({0.f,sz.y-bottom});ImGui::SetNextWindowSize({sz.x,bottom});
        ImGui::Begin("Status",nullptr,flags|ImGuiWindowFlags_NoTitleBar);ImGui::TextUnformatted(status_.c_str());ImGui::End();
    }

    void ensureTilemapEnabled() {
        if (!scene_.tilemap.enabled) {
            scene_.tilemap.enabled = true;
            scene_.tilemap.name = "Tilemap_World";
            scene_.tilemap.blockSize = 32;
            scene_.tilemap.resize(64, 36, 0);
        } else if (scene_.tilemap.cells.empty()) {
            scene_.tilemap.resize(scene_.tilemap.width, scene_.tilemap.height, 0);
        }
    }

    int ensureTileDefinition(const std::string& relTexture) {
        ensureTilemapEnabled();
        for (const auto& tile : scene_.tilemap.tiles)
            if (tile.texture == relTexture) return tile.id;
        int newId = 1;
        for (const auto& tile : scene_.tilemap.tiles) newId = std::max(newId, tile.id + 1);
        TileDefinition def;
        def.id = newId;
        def.texture = relTexture;
        def.name = fs::path(relTexture).stem().string();
        scene_.tilemap.tiles.push_back(def);
        std::sort(scene_.tilemap.tiles.begin(), scene_.tilemap.tiles.end(), [](const TileDefinition& a, const TileDefinition& b) { return a.id < b.id; });
        return newId;
    }

    const TileDefinition* selectedTileDefinition() const {
        return scene_.tilemap.tileById(selectedTileId_);
    }

    void tilemapPanel() {
        if (!scene_.tilemap.enabled) {
            ImGui::TextWrapped("Create a Tilemap to store a 2D array of block IDs, and paint tiles directly into the scene.");
            if (ImGui::Button("Create Tilemap")) {
                ensureTilemapEnabled();
                selectedId_ = kTilemapSelection;
                selectedTileId_ = 0;
                tilePaintMode_ = true;
            }
            return;
        }
        ImGui::Checkbox("Paint mode", &tilePaintMode_);
        ImGui::SameLine();
        ImGui::Checkbox("Eraser", &tileEraser_);
        ImGui::SameLine();
        if (ImGui::Button("Select Tilemap")) selectedId_ = kTilemapSelection;
        ImGui::Separator();
        if (ImGui::Selectable("0: Air", tileEraser_ || selectedTileId_ == 0)) {
            selectedTileId_ = 0; tileEraser_ = true; tilePaintMode_ = true;
        }
        ImGui::TextUnformatted("Registered block types");
        for (const auto& tile : scene_.tilemap.tiles) {
            ImGui::PushID(tile.id);
            if (ImGui::Selectable((std::to_string(tile.id) + ": " + tile.name).c_str(),
                                  selectedTileId_ == tile.id && !tileEraser_)) {
                selectedTileId_ = tile.id; tileEraser_ = false; tilePaintMode_ = true;
            }
            if (!tile.texture.empty()) {
                if (const sf::Texture* tex = textures_.get(projectDir_ / tile.texture)) {
                    ImGui::SameLine();
                    const auto s = tex->getSize();
                    const float factor = std::min(24.f/std::max(1u,s.x), 24.f/std::max(1u,s.y));
                    ImGui::Image(*tex, {s.x * factor, s.y * factor});
                }
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Add sprite texture as a block type");
        const auto all = assetsIn(projectDir_);
        if (all.empty()) ImGui::TextDisabled("Import a PNG under Assets first.");
        int buttonIndex = 0;
        for (const auto& path : all) {
            const std::string rel = "assets/" + path.filename().string();
            ImGui::PushID(buttonIndex);
            if (ImGui::SmallButton(("+ " + path.filename().string()).c_str())) {
                selectedTileId_ = ensureTileDefinition(rel);
                tileEraser_ = false;
                tilePaintMode_ = true;
                selectedId_ = kTilemapSelection;
            }
            ImGui::PopID();
            if (++buttonIndex % 3 != 0) ImGui::SameLine();
        }
        ImGui::Separator();
        ImGui::Text("Hover: (%d, %d)", hoveredTile_.x, hoveredTile_.y);
        if (scene_.tilemap.inBounds(hoveredTile_.x, hoveredTile_.y)) {
            const int value = scene_.tilemap.get(hoveredTile_.x, hoveredTile_.y);
            const auto* def = scene_.tilemap.tileById(value);
            ImGui::Text("Array[y][x]: %d (%s)", value, def ? def->name.c_str() : "Air");
        }
        ImGui::TextUnformatted("Top-left array preview (rows = y, columns = x)");
        std::ostringstream preview;
        const int rows = std::min(8, scene_.tilemap.height);
        const int cols = std::min(14, scene_.tilemap.width);
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) preview << scene_.tilemap.get(x, y) << ' ';
            preview << '\n';
        }
        ImGui::TextUnformatted(preview.str().c_str());
        ImGui::TextWrapped("Left-drag to paint, right-drag to erase, middle-drag to pan; turn off Paint Tiles to select and move sprites.");
    }

    void physicsInspector(PhysicsBody& p) {
        ImGui::Separator();
        if(ImGui::CollapsingHeader("Physics (Newtonian)",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Physics enabled",&p.enabled);
            const char* types[]={"Static","Dynamic","Kinematic"};
            int type=static_cast<int>(p.type);
            if(ImGui::Combo("Body type",&type,types,3))p.type=static_cast<BodyType>(type);
            ImGui::DragFloat2("Collider width/height",&p.size.x,1.f,1.f,4096.f);
            ImGui::DragFloat("Mass",&p.mass,.05f,.001f,100000.f);
            ImGui::DragFloat2("Velocity",&p.velocity.x,1.f);
            ImGui::DragFloat2("Acceleration",&p.acceleration.x,1.f);
            ImGui::DragFloat("Gravity scale",&p.gravityScale,.05f,-10.f,10.f);
            ImGui::DragFloat("Linear drag",&p.linearDrag,.02f,0.f,100.f);
            ImGui::DragFloat("Angular drag",&p.angularDrag,.02f,0.f,100.f);
            ImGui::SliderFloat("Friction",&p.friction,0.f,1.f);
            ImGui::SliderFloat("Restitution",&p.restitution,0.f,1.f);
            ImGui::Checkbox("Tile collisions",&p.collideWithTiles);
            ImGui::Checkbox("Object collisions",&p.collideWithBodies);
            ImGui::DragFloat("Inertia",&p.inertia,.05f,.001f,100000.f);
            ImGui::DragFloat("Angular velocity (deg/s)",&p.angularVelocity,.1f);
            ImGui::DragFloat("Angular acceleration",&p.angularAcceleration,.1f);
            ImGui::DragFloat("Rotating frame omega",&p.rotatingFrameRadiansPerSecond,.01f);
            ImGui::DragFloat2("Frame origin",&p.rotatingFrameOrigin.x,1.f);
            ImGui::TextWrapped("omega^2*r is an optional centrifugal pseudoforce in a rotating frame. Applied startup forces are used only on the first physics step.");
            ImGui::Separator();
            ImGui::DragFloat2("Startup force",&inspectorForce_.x,1.f);
            if(ImGui::SmallButton("Add force")) applyForce(p,inspectorForce_);
            ImGui::SameLine();
            if(ImGui::SmallButton("Impulse")) applyImpulse(p,inspectorForce_);
            ImGui::DragFloat("Startup torque",&inspectorTorque_,.1f);
            if(ImGui::SmallButton("Add torque")) applyTorque(p,inspectorTorque_);
        }
    }

    void inspector() {
        if (selectedId_ == kTilemapSelection) {
            ensureTilemapEnabled();
            drawPanelTitle("Tilemap properties");
            char tmName[128] = {};
            std::strncpy(tmName, scene_.tilemap.name.c_str(), sizeof(tmName)-1);
            if (ImGui::InputText("Name", tmName, sizeof(tmName))) scene_.tilemap.name = tmName;
            int dimensions[] = {scene_.tilemap.width, scene_.tilemap.height};
            if (ImGui::InputInt2("Array Size", dimensions)) {
                dimensions[0] = std::clamp(dimensions[0], 1, 4096);
                dimensions[1] = std::clamp(dimensions[1], 1, 4096);
                scene_.tilemap.resize(dimensions[0], dimensions[1], 0);
            }
            int blockSize = scene_.tilemap.blockSize;
            if (ImGui::InputInt("Block Size", &blockSize)) scene_.tilemap.blockSize = std::clamp(blockSize, 4, 256);
            ImGui::TextDisabled("To remove the Tilemap, select it and click Delete in Hierarchy.");
            ImGui::Checkbox("Paint mode", &tilePaintMode_);
            ImGui::Text("Tiles: %d", static_cast<int>(scene_.tilemap.tiles.size()));
            ImGui::Text("Array cells: %d x 3 layers", static_cast<int>(scene_.tilemap.cells.size()));
            const char* storage="2D Array (flat vector)";
            if(ImGui::BeginCombo("Storage",storage)) {
                ImGui::Selectable("2D Array (flat vector)",true);
                ImGui::TextDisabled("Other storage backends are not implemented.");
                ImGui::EndCombo();
            }
            if(ImGui::BeginCombo("Render Mode","Batched Textures")) {
                ImGui::Selectable("Batched Textures",true);
                ImGui::TextDisabled("Atlas packing is not implemented.");
                ImGui::EndCombo();
            }
            const char* chunkNames[]={"16 x 16","32 x 32","64 x 64","128 x 128","256 x 256"};
            int chunkValues[]={16,32,64,128,256};int chosen=2;
            for(int i=0;i<5;++i)if(scene_.tilemap.chunkSize==chunkValues[i])chosen=i;
            if(ImGui::Combo("Chunk Size",&chosen,chunkNames,5))scene_.tilemap.chunkSize=chunkValues[chosen];
            bool solid=true;
            for(auto& definition:scene_.tilemap.tiles)if(definition.id!=0&& !definition.solid){solid=false;break;}
            int collisions=solid?0:1;
            const char* collisionNames[]={"Solid Blocks","Pass-through Blocks"};
            if(ImGui::Combo("Collision",&collisions,collisionNames,2)){
                for(auto& def:scene_.tilemap.tiles)if(def.id!=0)def.solid=collisions==0;
            }
            ImGui::TextDisabled("Chunks are logical regions, not streamed from disk.");
            if (selectedTileId_ != 0) {
                if (const auto* def = selectedTileDefinition()) ImGui::Text("Selected tile: %d (%s)", def->id, def->name.c_str());
            } else {
                ImGui::Text("Selected tile: Air / Erase");
            }
            ImGui::Separator();
            ImGui::TextWrapped("Use the Tilemap tab below to choose tile textures and paint directly into the 2D array.");
            return;
        }
        if (auto* t=scene_.findText(selectedId_)) {
            drawPanelTitle("Bitmap text object");
            char textBuffer[512]{};std::strncpy(textBuffer,t->text.c_str(),sizeof(textBuffer)-1);
            if(ImGui::InputTextMultiline("Text",textBuffer,sizeof(textBuffer),ImVec2(-FLT_MIN,80)))t->text=textBuffer;
            char fontPath[256]{};std::strncpy(fontPath,t->bitmapFont.c_str(),sizeof(fontPath)-1);
            if(ImGui::InputText("Font path",fontPath,sizeof(fontPath)))t->bitmapFont=fontPath;
            if(ImGui::Button("Use current font")) t->bitmapFont="assets/fonts/"+std::string(fontFileName_)+".sffont.json";
            ImGui::DragFloat2("Position",&t->position.x,1.f);
            ImGui::DragFloat("Pixel scale",&t->pixelScale,.1f,.5f,64.f);
            float color[4]={t->color.r/255.f,t->color.g/255.f,t->color.b/255.f,t->color.a/255.f};
            if(ImGui::ColorEdit4("Color",color))t->color=sf::Color(static_cast<std::uint8_t>(color[0]*255),static_cast<std::uint8_t>(color[1]*255),static_cast<std::uint8_t>(color[2]*255),static_cast<std::uint8_t>(color[3]*255));
            if(ImGui::Button("Delete text object")) {
                scene_.textObjects.erase(std::remove_if(scene_.textObjects.begin(),scene_.textObjects.end(),[&](const TextObject& obj){return obj.id==selectedId_;}),scene_.textObjects.end());
                selectedId_=-1;
            }
            return;
        }
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
            ImGui::Separator();
            ImGui::Separator();
            ImGui::Text("World physics / timing");
            ImGui::DragFloat2("Gravity px/s^2",&scene_.physics.gravity.x,1.f);
            ImGui::InputInt("Fixed Hz",&scene_.physics.fixedHz);
            scene_.physics.fixedHz=std::clamp(scene_.physics.fixedHz,30,480);
            ImGui::InputInt("Max FPS",&scene_.physics.maxRenderFps);
            scene_.physics.maxRenderFps=std::clamp(scene_.physics.maxRenderFps,30,1000);
            ImGui::TextWrapped("Physics is simulated in game runtime at fixed Hz; the editor is capped at 480 FPS.");
            ImGui::TextWrapped("Select Scene, Tilemap_World, or a sprite in the hierarchy or viewport to edit its properties.");
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
        physicsInspector(obj->physics);
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
            newObj.physics = copy.physics;
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
        if (ImGui::SmallButton("Import image...")) importImage();
        ImGui::SameLine();ImGui::TextDisabled("Drag into scene");
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
            const int perRow=assetsGridView_?3:1;
            if (i+1 < static_cast<int>(all.size()) && (i+1)%perRow !=0) ImGui::SameLine(0.f,8.f);
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
        ImGui::TextDisabled("2D Scene  |  %.0f%% zoom  |  Pan: middle drag / Shift + left drag  |  Layer %d",100.f/zoom_,tileLayer_);
        ImGui::SameLine();
        if(ImGui::SmallButton("1:1"))zoom_=1.f;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const unsigned rw = static_cast<unsigned>(std::max(1.f, available.x));
        const unsigned rh = static_cast<unsigned>(std::max(1.f, available.y));
        if (rw < 2 || rh < 2) return;
        if (viewport_.getSize() != sf::Vector2u{rw,rh}) {
            if (!viewport_.resize({rw,rh})) { notify("Could not resize scene render texture."); return; }
        }
        viewportW_ = static_cast<float>(rw);
        viewportH_ = static_cast<float>(rh);
        const sf::View view(camera_, {viewportW_*zoom_,viewportH_*zoom_});
        viewport_.setView(view);
        const Scene& active = scene_;
        viewport_.clear(active.background);
        drawScene(viewport_, active, projectDir_, textures_, selectedId_, grid_, zoom_, gridSizePreview_);
        if(showColliders_) {
            for(const auto& obj:active.objects) if(obj.physics.enabled) {
                sf::RectangleShape collision(obj.physics.size);
                collision.setPosition(obj.position);
                collision.setFillColor(sf::Color::Transparent);
                collision.setOutlineColor(sf::Color(70,240,170,220));
                collision.setOutlineThickness(1.f*zoom_);
                viewport_.draw(collision);
            }
        }
        viewport_.display();
        ImGui::Image(viewport_, {viewportW_,viewportH_});
        const bool hovered = ImGui::IsItemHovered();
        const auto start = ImGui::GetItemRectMin();
        auto& io = ImGui::GetIO();
        auto worldAt = [&](const ImVec2 mouse) -> sf::Vector2f {
            return {camera_.x + (mouse.x-start.x-viewportW_/2.f)*zoom_,
                    camera_.y + (mouse.y-start.y-viewportH_/2.f)*zoom_};
        };
        auto screenFromWorld = [&](sf::Vector2f world) -> ImVec2 {
            return {start.x + (world.x - camera_.x) / zoom_ + viewportW_ / 2.f,
                    start.y + (world.y - camera_.y) / zoom_ + viewportH_ / 2.f};
        };
        hoveredTile_ = {-1, -1};
        if (hovered) {
            if (io.MouseWheel != 0.f) {
                const sf::Vector2f before = worldAt(io.MousePos);
                zoom_ = std::clamp(zoom_*std::pow(0.85f, io.MouseWheel),0.2f,6.f);
                const sf::Vector2f after = worldAt(io.MousePos);
                camera_ += before-after;
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
                camera_.x -= io.MouseDelta.x * zoom_;
                camera_.y -= io.MouseDelta.y * zoom_;
                draggingObject_ = false;
            }
            if (scene_.tilemap.enabled) {
                const sf::Vector2f at = worldAt(io.MousePos);
                hoveredTile_ = worldToTileCell(scene_.tilemap, at);
                if (scene_.tilemap.inBounds(hoveredTile_.x, hoveredTile_.y)) {
                    const auto bounds = tileCellBounds(scene_.tilemap, hoveredTile_.x, hoveredTile_.y);
                    const ImVec2 p0 = screenFromWorld(bounds.position);
                    const ImVec2 p1 = screenFromWorld({bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y});
                    ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(255, 215, 0, 220), 0.f, 0, 2.f);
                }
            }
        }

        const bool tileEditing=scene_.tilemap.enabled && (tilePaintMode_ || selectedId_==kTilemapSelection);
        const bool panModifier=io.KeyShift || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool cellValid=scene_.tilemap.enabled && scene_.tilemap.inBounds(hoveredTile_.x,hoveredTile_.y);
        if (!builder_.busy() && hovered && tileEditing && cellValid && !panModifier) {
            auto paintCell=[&](int x,int y,int value){
                for(int yy=y;yy<y+brushSize_;++yy)
                    for(int xx=x;xx<x+brushSize_;++xx) scene_.tilemap.setLayer(tileLayer_,xx,yy,value);
            };
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tileTool_==TileTool::Eyedropper) {
                selectedTileId_=scene_.tilemap.getLayer(tileLayer_,hoveredTile_.x,hoveredTile_.y);
                tileTool_=TileTool::Paint;
            } else if(tileTool_==TileTool::Rectangle) {
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) rectangleStart_=hoveredTile_;
                if(ImGui::IsMouseReleased(ImGuiMouseButton_Left) && rectangleStart_.x>=0) {
                    const int x0=std::min(rectangleStart_.x,hoveredTile_.x);
                    const int x1=std::max(rectangleStart_.x,hoveredTile_.x);
                    const int y0=std::min(rectangleStart_.y,hoveredTile_.y);
                    const int y1=std::max(rectangleStart_.y,hoveredTile_.y);
                    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
                        scene_.tilemap.setLayer(tileLayer_,x,y,selectedTileId_);
                    rectangleStart_={-1,-1};
                }
            } else if(ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                paintCell(hoveredTile_.x,hoveredTile_.y,tileTool_==TileTool::Erase?0:selectedTileId_);
            }
            if(ImGui::IsMouseDown(ImGuiMouseButton_Right)) paintCell(hoveredTile_.x,hoveredTile_.y,0);
        } else if(!builder_.busy() && !tileEditing && !panModifier) {
            if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const sf::Vector2f at=worldAt(io.MousePos);
                selectedId_=findObjectAt(scene_,at,projectDir_,textures_);
                draggingObject_=selectedId_>=0;
                if(auto* o=scene_.find(selectedId_))objectDragOffset_=o->position-at;
            }
            if(draggingObject_ && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                if(auto* o=scene_.find(selectedId_))o->position=worldAt(io.MousePos)+objectDragOffset_;
            }
        }
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))draggingObject_=false;
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
