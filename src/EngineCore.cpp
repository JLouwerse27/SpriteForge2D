#include "EngineCore.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace forge {
using nlohmann::json;

Object& Scene::add(const std::string& name, const std::string& texture, sf::Vector2f position) {
    Object o;
    o.id = nextId++;
    o.name = name;
    o.texture = texture;
    o.position = position;
    objects.push_back(o);
    return objects.back();
}
Object* Scene::find(int id) {
    for (auto& o : objects) if (o.id == id) return &o;
    return nullptr;
}
const Object* Scene::find(int id) const {
    for (const auto& o : objects) if (o.id == id) return &o;
    return nullptr;
}
bool Scene::remove(int id) {
    const auto old = objects.size();
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [id](const Object& o) { return o.id == id; }), objects.end());
    return old != objects.size();
}

const sf::Texture* TextureCache::get(const fs::path& path) {
    const std::string key = path.lexically_normal().string();
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.get();
    auto tex = std::make_unique<sf::Texture>();
    if (!tex->loadFromFile(path)) return nullptr;
    tex->setSmooth(false);
    const sf::Texture* ptr = tex.get();
    textures_.emplace(key, std::move(tex));
    return ptr;
}
void TextureCache::invalidate(const fs::path& path) { textures_.erase(path.lexically_normal().string()); }
void TextureCache::clear() { textures_.clear(); }

bool saveScene(const Scene& scene, const fs::path& path, std::string& error) {
    try {
        json j;
        j["version"] = 1;
        j["title"] = scene.title;
        j["size"] = {scene.width, scene.height};
        j["background"] = {scene.background.r, scene.background.g, scene.background.b};
        j["objects"] = json::array();
        for (const auto& o : scene.objects) {
            j["objects"].push_back({
                {"id", o.id}, {"name", o.name}, {"texture", o.texture},
                {"position", {o.position.x, o.position.y}},
                {"scale", {o.scale.x, o.scale.y}}, {"rotation", o.rotation},
                {"visible", o.visible}, {"controllable", o.controllable},
                {"moveSpeed", o.moveSpeed}, {"behavior", o.behavior},
                {"behaviorValues", o.behaviorValues}
            });
        }
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("Could not open scene for writing");
        out << j.dump(2) << '\n';
        if (!out) throw std::runtime_error("Could not finish writing scene");
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what(); return false;
    }
}

bool loadScene(Scene& scene, const fs::path& path, std::string& error) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open " + path.string());
        const json j = json::parse(in);
        if (j.value("version", 0) != 1) throw std::runtime_error("Unsupported scene version");
        Scene loaded;
        loaded.title = j.value("title", "My Game");
        loaded.width = std::clamp(j.at("size").at(0).get<unsigned>(), 160u, 3840u);
        loaded.height = std::clamp(j.at("size").at(1).get<unsigned>(), 120u, 2160u);
        const auto& c = j.at("background");
        loaded.background = sf::Color(
            static_cast<std::uint8_t>(std::clamp(c.at(0).get<int>(), 0, 255)),
            static_cast<std::uint8_t>(std::clamp(c.at(1).get<int>(), 0, 255)),
            static_cast<std::uint8_t>(std::clamp(c.at(2).get<int>(), 0, 255)));
        for (const auto& item : j.at("objects")) {
            Object o;
            o.id = item.at("id").get<int>();
            o.name = item.value("name", "Sprite");
            o.texture = item.value("texture", "");
            // Only load assets within the project tree.
            const fs::path asset = fs::path(o.texture).lexically_normal();
            if (asset.is_absolute() || asset.empty() ||
                *asset.begin() != fs::path("assets") ||
                o.texture.find("..") != std::string::npos)
                throw std::runtime_error("Scene contains invalid asset path");
            o.position = {item.at("position").at(0).get<float>(), item.at("position").at(1).get<float>()};
            o.scale = {item.at("scale").at(0).get<float>(), item.at("scale").at(1).get<float>()};
            o.rotation = item.value("rotation", 0.f);
            o.visible = item.value("visible", true);
            o.controllable = item.value("controllable", false);
            o.moveSpeed = item.value("moveSpeed", 230.f);
            o.behavior = item.value("behavior", std::string{});
            if (item.contains("behaviorValues"))
                o.behaviorValues = item.at("behaviorValues").get<std::map<std::string, float>>();
            if (o.id <= 0 || o.id > 10000000) throw std::runtime_error("Invalid object ID");
            if (loaded.find(o.id)) throw std::runtime_error("Duplicate object ID");
            loaded.nextId = std::max(loaded.nextId, o.id + 1);
            loaded.objects.push_back(o);
            if (loaded.objects.size() > 10000) throw std::runtime_error("Too many objects");
        }
        scene = std::move(loaded);
        error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

static sf::Sprite configureSprite(const Object& o, const sf::Texture& texture) {
    sf::Sprite sprite(texture);
    sprite.setPosition(o.position);
    sprite.setScale(o.scale);
    sprite.setRotation(sf::degrees(o.rotation));
    return sprite;
}

void drawScene(sf::RenderTarget& target, const Scene& scene, const fs::path& root,
               TextureCache& textures, int selectedId, bool grid, float pixelScale) {
    if (grid) {
        const sf::View view = target.getView();
        const auto size = view.getSize();
        const auto ctr = view.getCenter();
        constexpr int step = 64;
        const int left = static_cast<int>(std::floor((ctr.x-size.x/2.f)/step))*step;
        const int top = static_cast<int>(std::floor((ctr.y-size.y/2.f)/step))*step;
        for (int x = left; x < ctr.x + size.x/2.f; x += step) {
            sf::RectangleShape line({std::max(0.4f,pixelScale), size.y+step});
            line.setFillColor(sf::Color(85, 95, 115, 55));
            line.setPosition({static_cast<float>(x), ctr.y-size.y/2.f});
            target.draw(line);
        }
        for (int y = top; y < ctr.y + size.y/2.f; y += step) {
            sf::RectangleShape line({size.x+step, std::max(0.4f,pixelScale)});
            line.setFillColor(sf::Color(85, 95, 115, 55));
            line.setPosition({ctr.x-size.x/2.f, static_cast<float>(y)});
            target.draw(line);
        }
        sf::RectangleShape boundary({static_cast<float>(scene.width),static_cast<float>(scene.height)});
        boundary.setFillColor(sf::Color::Transparent);
        boundary.setOutlineColor(sf::Color(210,210,235,130));
        boundary.setOutlineThickness(pixelScale);
        target.draw(boundary);
    }
    for (const auto& obj : scene.objects) {
        if (!obj.visible) continue;
        const sf::Texture* tex = textures.get(root / obj.texture);
        if (tex) {
            auto spr = configureSprite(obj, *tex);
            target.draw(spr);
            if (obj.id == selectedId) {
                const auto bounds = spr.getGlobalBounds();
                sf::RectangleShape outline(bounds.size);
                outline.setPosition(bounds.position);
                outline.setFillColor(sf::Color::Transparent);
                outline.setOutlineColor(sf::Color(255, 201, 76));
                outline.setOutlineThickness(2.f*pixelScale);
                target.draw(outline);
            }
        } else {
            sf::RectangleShape missing({32.f,32.f});
            missing.setPosition(obj.position);
            missing.setFillColor(sf::Color::Magenta);
            target.draw(missing);
        }
    }
}

int findObjectAt(const Scene& scene, sf::Vector2f point, const fs::path& root, TextureCache& textures) {
    for (auto it = scene.objects.rbegin(); it != scene.objects.rend(); ++it) {
        if (!it->visible) continue;
        const auto* texture = textures.get(root / it->texture);
        if (texture && configureSprite(*it, *texture).getGlobalBounds().contains(point))
            return it->id;
    }
    return -1;
}

void updatePlayerControls(Scene& scene, float dt, bool left, bool right, bool up, bool down) {
    if (dt <= 0.f) return;
    float dx = static_cast<float>(right) - static_cast<float>(left);
    float dy = static_cast<float>(down) - static_cast<float>(up);
    const float len = std::sqrt(dx*dx + dy*dy);
    if (len > 0.f) { dx /= len; dy /= len; }
    for (auto& obj : scene.objects) {
        if (!obj.controllable || !obj.behavior.empty()) continue;
        obj.position.x = std::clamp(obj.position.x + dx*obj.moveSpeed*dt, 0.f, static_cast<float>(scene.width));
        obj.position.y = std::clamp(obj.position.y + dy*obj.moveSpeed*dt, 0.f, static_cast<float>(scene.height));
    }
}

fs::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Cannot locate executable");
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}
} // namespace forge
