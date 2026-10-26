#include "EngineCore.h"
#include "Behaviors.h"
#include "GameBehaviors.h"
#include <imgui.h>
#include <imgui-SFML.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
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

fs::path chooseFolder(HWND owner = nullptr) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = L"Choose a folder for the exported game";
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
    BehaviorSystem behaviorSystem_{registry_};
    Scene scene_;
    Scene playScene_;
    fs::path projectDir_;
    fs::path currentSceneFile_;
    std::string status_ = "Ready. Import a PNG or drag a sprite from Assets into the viewport.";
    int selectedId_ = -1;
    int inspectorId_ = -1;
    char objectName_[256] = {};
    char exportName_[128] = "MyGame";
    bool playing_ = false;
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
        registerGameBehaviors(registry_);
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
            if (playing_) {
                try {
                    const InputState input = (window_.hasFocus() && !ImGui::GetIO().WantTextInput)
                        ? readKeyboardInput() : InputState{};
                    behaviorSystem_.update(playScene_, input, dt);
                } catch (const std::exception& e) {
                    notify(std::string("C++ behavior error: ") + e.what());
                    behaviorSystem_.stop(playScene_);
                    playing_ = false;
                }
            }
            menuBar();
            mainPanels();
            if (showAbout_) {
                ImGui::Begin("About SpriteForge", &showAbout_);
                ImGui::TextWrapped("SpriteForge 2D is a minimal visual scene editor written with SFML 3 and Dear ImGui. Exported games run in a separate native Windows executable.");
                ImGui::TextWrapped("C++ behaviors are statically compiled into both the editor and exported game. Modify game/GameBehaviors.cpp, then rebuild and restart the editor.");
                ImGui::End();
            }
            if (showScriptHelp_) {
                ImGui::Begin("C++ Behaviors (Option A)", &showScriptHelp_);
                ImGui::TextWrapped("Game code: game/GameBehaviors.cpp");
                ImGui::TextWrapped("Add a class derived from forge::Behavior and register it in registerGameBehaviors().");
                ImGui::TextWrapped("Close the editor, run build_windows.bat, and reopen it to test your new compiled code.");
                ImGui::TextWrapped("The Editor and GameRuntime are both linked with GameCode. Export copies that precompiled runtime; scene and property changes alone require no rebuild.");
                ImGui::End();
            }
            window_.clear(sf::Color(18, 20, 25));
            ImGui::SFML::Render(window_);
            window_.display();
        }
        ImGui::SFML::Shutdown();
    }

private:
    void notify(const std::string& text) { status_ = text; }

    void saveAs() {
        const fs::path dest = chooseFile(true, window_.getNativeHandle());
        if (dest.empty()) return;
        try {
            copyProjectAssets(scene_, projectDir_, dest.parent_path());
            projectDir_ = dest.parent_path();
            currentSceneFile_ = dest;
            std::string err;
            if (!saveScene(scene_, dest, err)) throw std::runtime_error(err);
            textures_.clear();
            notify("Saved scene: " + dest.string());
        } catch (const std::exception& e) { notify(std::string("Save failed: ") + e.what()); }
    }

    void save() {
        if (currentSceneFile_.empty()) { saveAs(); return; }
        std::string err;
        if (!saveScene(scene_, currentSceneFile_, err)) notify("Save failed: " + err);
        else notify("Saved scene: " + currentSceneFile_.string());
    }

    void open() {
        const fs::path dest = chooseFile(false, window_.getNativeHandle());
        if (dest.empty()) return;
        Scene loaded;
        std::string err;
        if (!loadScene(loaded, dest, err)) { notify("Open failed: " + err); return; }
        if (playing_) { behaviorSystem_.stop(playScene_); playing_ = false; }
        scene_ = std::move(loaded);
        projectDir_ = dest.parent_path();
        currentSceneFile_ = dest;
        textures_.clear();
        selectedId_ = -1;
        inspectorId_ = -1;
        playing_ = false;
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
        if (playing_) { notify("Stop Play mode before exporting."); return; }
        const auto dest = chooseFolder(window_.getNativeHandle());
        if (dest.empty()) return;
        try {
            const fs::path runtime = executableDirectory() / "GameRuntime.exe";
            if (!fs::is_regular_file(runtime)) throw std::runtime_error("GameRuntime.exe is missing beside Editor.exe. Build the GameRuntime target first.");
            const std::string name = safeGameName(exportName_);
            const fs::path out = dest / name;
            fs::create_directories(out);
            // Fail if a scene references a behavior not compiled into this build.
            validateBehaviors(scene_, registry_);
            // Validate/copy assets first; then write the scene and executable.
            copyProjectAssets(scene_, projectDir_, out);
            std::string err;
            if (!saveScene(scene_, out / "scene.json", err)) throw std::runtime_error(err);
            fs::copy_file(runtime, out / (name + ".exe"), fs::copy_options::overwrite_existing);
            notify("Exported! Open " + (out / (name + ".exe")).string());
        } catch (const std::exception& e) { notify(std::string("Export failed: ") + e.what()); }
    }

    void menuBar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New scene")) {
                    if (playing_) behaviorSystem_.stop(playScene_);
                    scene_ = Scene{};
                    selectedId_ = -1;
                    inspectorId_ = -1;
                    currentSceneFile_.clear();
                    playing_ = false;
                    camera_ = {480.f, 270.f};
                    notify("New scene. Use File > Save as to choose its location.");
                }
                if (ImGui::MenuItem("Open scene...")) open();
                if (ImGui::MenuItem("Save scene")) save();
                if (ImGui::MenuItem("Save as...")) saveAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Import image...")) importImage();
                ImGui::Separator();
                if (ImGui::MenuItem("Export EXE...", nullptr, false, !playing_)) exportGame();
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
                if (ImGui::MenuItem("C++ Behaviors (Option A)")) showScriptHelp_ = true;
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
        const float assetHeight = std::clamp(size.y * 0.24f, 150.f, 240.f);
        const float centerW = std::max(100.f, size.x-leftWidth-rightWidth);
        const float centerH = std::max(100.f, size.y-top-assetHeight-bottom);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos({0.f, top});
        ImGui::SetNextWindowSize({leftWidth, size.y-top-bottom});
        ImGui::Begin("Hierarchy", nullptr, flags);
        drawPanelTitle("Objects");
        ImGui::BeginDisabled(playing_);
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
        ImGui::Begin("Assets", nullptr, flags);
        assetBrowser();
        ImGui::End();

        ImGui::SetNextWindowPos({0.f,size.y-bottom});
        ImGui::SetNextWindowSize({size.x,bottom});
        ImGui::Begin("Status", nullptr, flags | ImGuiWindowFlags_NoTitleBar);
        ImGui::TextUnformatted(status_.c_str());
        ImGui::End();
    }

    void inspector() {
        if (playing_) {
            drawPanelTitle("Play mode");
            ImGui::TextWrapped("Stop to edit. Compiled C++ behaviors are running on a temporary scene copy. WASD/arrow keys control MoveWithKeys.");
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
        ImGui::TextUnformatted("C++ Behavior (Option A)");
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
                ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "Missing compiled class: %s", obj->behavior.c_str());
                ImGui::TextWrapped("Register the class in game/GameBehaviors.cpp and rebuild.");
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
        ImGui::BeginDisabled(playing_);
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

    void sceneViewport() {
        if (ImGui::Button(playing_ ? "Stop" : "Play")) {
            if (playing_) {
                behaviorSystem_.stop(playScene_);
                playing_ = false;
            } else {
                try {
                    validateBehaviors(scene_, registry_);
                    playScene_ = scene_;
                    behaviorSystem_.start(playScene_);
                    playing_ = true;
                    selectedId_ = -1;
                    notify("Play mode (compiled C++ game behaviors). Stop to return to editor.");
                } catch (const std::exception& e) {
                    notify(std::string("Cannot play: ") + e.what());
                    behaviorSystem_.stop(playScene_);
                    playing_ = false;
                }
            }
        }
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
        if (ImGui::Button("Export EXE") && !playing_) exportGame();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const unsigned rw = static_cast<unsigned>(std::max(1.f, available.x));
        const unsigned rh = static_cast<unsigned>(std::max(1.f, available.y));
        if (rw < 2 || rh < 2) return;
        if (viewport_.getSize() != sf::Vector2u{rw,rh}) viewport_.resize({rw,rh});
        viewportW_ = static_cast<float>(rw);
        viewportH_ = static_cast<float>(rh);
        const sf::View view(camera_, {viewportW_*zoom_,viewportH_*zoom_});
        viewport_.setView(view);
        const Scene& active = playing_ ? playScene_ : scene_;
        viewport_.clear(active.background);
        drawScene(viewport_, active, projectDir_, textures_, playing_ ? -1 : selectedId_,
                  !playing_ && grid_, zoom_);
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
        if (!playing_ && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const sf::Vector2f at = worldAt(io.MousePos);
            selectedId_ = findObjectAt(scene_, at, projectDir_, textures_);
            draggingObject_ = selectedId_ >= 0;
            if (auto* o = scene_.find(selectedId_)) objectDragOffset_ = o->position-at;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) draggingObject_ = false;
        if (!playing_ && draggingObject_ && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            if (auto* o = scene_.find(selectedId_)) o->position = worldAt(io.MousePos)+objectDragOffset_;
        }
        if (!playing_ && ImGui::BeginDragDropTarget()) {
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
