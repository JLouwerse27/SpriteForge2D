#include "EngineCore.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace forge {
using nlohmann::json;

void TilemapData::resize(int newWidth, int newHeight, int fillValue) {
    newWidth = std::clamp(newWidth, 1, 4096);
    newHeight = std::clamp(newHeight, 1, 4096);
    std::vector<int> next(static_cast<std::size_t>(newWidth) * static_cast<std::size_t>(newHeight), fillValue);
    if (cells.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        const int keepW = std::min(width, newWidth);
        const int keepH = std::min(height, newHeight);
        for (int y = 0; y < keepH; ++y) {
            for (int x = 0; x < keepW; ++x)
                next[static_cast<std::size_t>(y * newWidth + x)] = cells[static_cast<std::size_t>(y * width + x)];
        }
    }
    width = newWidth;
    height = newHeight;
    cells = std::move(next);
}

bool TilemapData::inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
int TilemapData::index(int x, int y) const { return y * width + x; }
int TilemapData::get(int x, int y) const {
    if (!inBounds(x, y)) return 0;
    const std::size_t at = static_cast<std::size_t>(index(x, y));
    return at < cells.size() ? cells[at] : 0;
}
void TilemapData::set(int x, int y, int value) {
    if (!inBounds(x, y)) return;
    const std::size_t at = static_cast<std::size_t>(index(x, y));
    if (at < cells.size()) cells[at] = value;
}
const TileDefinition* TilemapData::tileById(int id) const {
    for (const auto& tile : tiles) if (tile.id == id) return &tile;
    return nullptr;
}

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

namespace {
void validateAssetPath(const std::string& path) {
    const fs::path asset = fs::path(path).lexically_normal();
    if (asset.is_absolute() || asset.empty() || *asset.begin() != fs::path("assets") ||
        path.find("..") != std::string::npos)
        throw std::runtime_error("Scene contains invalid asset path");
}
}

bool saveScene(const Scene& scene, const fs::path& path, std::string& error) {
    try {
        json j;
        j["version"] = 2;
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
        if (scene.tilemap.enabled) {
            json tm;
            tm["enabled"] = true;
            tm["name"] = scene.tilemap.name;
            tm["width"] = scene.tilemap.width;
            tm["height"] = scene.tilemap.height;
            tm["blockSize"] = scene.tilemap.blockSize;
            tm["cells"] = scene.tilemap.cells;
            tm["tiles"] = json::array();
            for (const auto& tile : scene.tilemap.tiles) {
                tm["tiles"].push_back({{"id", tile.id}, {"name", tile.name}, {"texture", tile.texture}, {"solid", tile.solid}});
            }
            j["tilemap"] = std::move(tm);
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
        const int version = j.value("version", 1);
        if (version != 1 && version != 2) throw std::runtime_error("Unsupported scene version");
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
            validateAssetPath(o.texture);
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
        if (j.contains("tilemap")) {
            const auto& tm = j.at("tilemap");
            loaded.tilemap.enabled = tm.value("enabled", true);
            loaded.tilemap.name = tm.value("name", std::string{"Tilemap_World"});
            loaded.tilemap.width = std::clamp(tm.value("width", 64), 1, 4096);
            loaded.tilemap.height = std::clamp(tm.value("height", 36), 1, 4096);
            loaded.tilemap.blockSize = std::clamp(tm.value("blockSize", 32), 4, 256);
            loaded.tilemap.cells = tm.value("cells", std::vector<int>{});
            const std::size_t required = static_cast<std::size_t>(loaded.tilemap.width) * static_cast<std::size_t>(loaded.tilemap.height);
            if (loaded.tilemap.cells.size() != required) loaded.tilemap.cells.assign(required, 0);
            loaded.tilemap.tiles.clear();
            if (tm.contains("tiles")) {
                for (const auto& item : tm.at("tiles")) {
                    TileDefinition t;
                    t.id = item.value("id", 0);
                    t.name = item.value("name", std::string{});
                    t.texture = item.value("texture", std::string{});
                    t.solid = item.value("solid", true);
                    if (t.id < 0 || t.id > 1000000) throw std::runtime_error("Invalid tile ID");
                    if (t.id != 0) validateAssetPath(t.texture);
                    loaded.tilemap.tiles.push_back(std::move(t));
                }
            }
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

sf::Vector2i worldToTileCell(const TilemapData& tilemap, sf::Vector2f world) {
    if (!tilemap.enabled || tilemap.blockSize <= 0) return {-1, -1};
    return {static_cast<int>(std::floor(world.x / tilemap.blockSize)),
            static_cast<int>(std::floor(world.y / tilemap.blockSize))};
}

sf::FloatRect tileCellBounds(const TilemapData& tilemap, int x, int y) {
    return {{static_cast<float>(x * tilemap.blockSize), static_cast<float>(y * tilemap.blockSize)},
            {static_cast<float>(tilemap.blockSize), static_cast<float>(tilemap.blockSize)}};
}

void drawScene(sf::RenderTarget& target, const Scene& scene, const fs::path& root,
               TextureCache& textures, int selectedId, bool grid, float pixelScale) {
    if (scene.tilemap.enabled && scene.tilemap.blockSize > 0) {
        const sf::View view = target.getView();
        const auto size = view.getSize();
        const auto ctr = view.getCenter();
        const float leftF = ctr.x - size.x / 2.f;
        const float topF = ctr.y - size.y / 2.f;
        const float rightF = ctr.x + size.x / 2.f;
        const float bottomF = ctr.y + size.y / 2.f;
        const int left = std::max(0, static_cast<int>(std::floor(leftF / scene.tilemap.blockSize)) - 1);
        const int top = std::max(0, static_cast<int>(std::floor(topF / scene.tilemap.blockSize)) - 1);
        const int right = std::min(scene.tilemap.width - 1, static_cast<int>(std::ceil(rightF / scene.tilemap.blockSize)) + 1);
        const int bottom = std::min(scene.tilemap.height - 1, static_cast<int>(std::ceil(bottomF / scene.tilemap.blockSize)) + 1);
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                const int id = scene.tilemap.get(x, y);
                if (id == 0) continue;
                const TileDefinition* def = scene.tilemap.tileById(id);
                if (!def || def->texture.empty()) continue;
                if (const sf::Texture* tex = textures.get(root / def->texture)) {
                    sf::Sprite sprite(*tex);
                    sprite.setPosition({static_cast<float>(x * scene.tilemap.blockSize), static_cast<float>(y * scene.tilemap.blockSize)});
                    const auto ts = tex->getSize();
                    if (ts.x > 0 && ts.y > 0) sprite.setScale({scene.tilemap.blockSize / static_cast<float>(ts.x), scene.tilemap.blockSize / static_cast<float>(ts.y)});
                    target.draw(sprite);
                }
            }
        }
        if (selectedId == -2) {
            sf::RectangleShape outline({static_cast<float>(scene.tilemap.width * scene.tilemap.blockSize), static_cast<float>(scene.tilemap.height * scene.tilemap.blockSize)});
            outline.setFillColor(sf::Color::Transparent);
            outline.setOutlineColor(sf::Color(102, 202, 255, 180));
            outline.setOutlineThickness(2.f * pixelScale);
            target.draw(outline);
        }
    }

    if (grid) {
        const sf::View view = target.getView();
        const auto size = view.getSize();
        const auto ctr = view.getCenter();
        int step = 64;
        if (scene.tilemap.enabled && scene.tilemap.blockSize > 0) step = scene.tilemap.blockSize;
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