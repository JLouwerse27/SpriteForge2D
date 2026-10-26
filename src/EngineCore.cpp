#include "EngineCore.h"
#include "BitmapFont.h"
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
    // Additional layers preserve their overlapping regions when resized.
    const int prevWidth = width, prevHeight = height;
    auto resized = [&](const std::vector<int>& old) {
        std::vector<int> result(static_cast<size_t>(newWidth)*static_cast<size_t>(newHeight), 0);
        if (old.size()==static_cast<size_t>(prevWidth)*static_cast<size_t>(prevHeight)) {
            for(int y=0;y<std::min(prevHeight,newHeight);++y)
                for(int x=0;x<std::min(prevWidth,newWidth);++x)
                    result[static_cast<size_t>(y*newWidth+x)] = old[static_cast<size_t>(y*prevWidth+x)];
        }
        return result;
    };
    layer1 = resized(layer1);
    layer2 = resized(layer2);
    cells = std::move(next);
    width = newWidth;
    height = newHeight;
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
int TilemapData::getLayer(int layer, int x, int y) const {
    if(!inBounds(x,y))return 0;
    const auto* arr = layer==1?&layer1:layer==2?&layer2:&cells;
    const size_t i=static_cast<size_t>(index(x,y));
    return i<arr->size()?(*arr)[i]:0;
}
void TilemapData::setLayer(int layer, int x, int y, int value) {
    if(!inBounds(x,y))return;
    auto* arr=layer==1?&layer1:layer==2?&layer2:&cells;
    if(arr->size()!=cells.size())arr->resize(cells.size(),0);
    (*arr)[static_cast<size_t>(index(x,y))]=std::max(0,value);
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
TextObject* Scene::findText(int id) {
    for (auto& t : textObjects) if (t.id==id) return &t;
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
                {"behaviorValues", o.behaviorValues},
                {"physics", {
                    {"enabled",o.physics.enabled}, {"type",static_cast<int>(o.physics.type)},
                    {"size",{o.physics.size.x,o.physics.size.y}}, {"mass",o.physics.mass},
                    {"velocity",{o.physics.velocity.x,o.physics.velocity.y}},
                    {"acceleration",{o.physics.acceleration.x,o.physics.acceleration.y}},
                    {"force",{o.physics.force.x,o.physics.force.y}}, {"torque",o.physics.torque},
                    {"gravityScale",o.physics.gravityScale},{"linearDrag",o.physics.linearDrag},
                    {"angularDrag",o.physics.angularDrag},{"friction",o.physics.friction},
                    {"restitution",o.physics.restitution},{"inertia",o.physics.inertia},
                    {"angularVelocity",o.physics.angularVelocity},
                    {"angularAcceleration",o.physics.angularAcceleration},
                    {"collideWithTiles",o.physics.collideWithTiles},
                    {"collideWithBodies",o.physics.collideWithBodies},
                    {"rotatingFrameRadiansPerSecond",o.physics.rotatingFrameRadiansPerSecond},
                    {"rotatingFrameOrigin",{o.physics.rotatingFrameOrigin.x,o.physics.rotatingFrameOrigin.y}}
                }}
            });
        }
        j["physicsSettings"]={{"gravity",{scene.physics.gravity.x,scene.physics.gravity.y}},
                              {"fixedHz",scene.physics.fixedHz},{"maxRenderFps",scene.physics.maxRenderFps}};
        j["textObjects"]=json::array();
        for(const auto& t:scene.textObjects)
            j["textObjects"].push_back({{"id",t.id},{"name",t.name},{"text",t.text},
                {"bitmapFont",t.bitmapFont},{"position",{t.position.x,t.position.y}},
                {"pixelScale",t.pixelScale},{"color",{t.color.r,t.color.g,t.color.b,t.color.a}}});
        if (scene.tilemap.enabled) {
            json tm;
            tm["enabled"] = true;
            tm["name"] = scene.tilemap.name;
            tm["width"] = scene.tilemap.width;
            tm["height"] = scene.tilemap.height;
            tm["blockSize"] = scene.tilemap.blockSize;
            tm["cells"] = scene.tilemap.cells;
            tm["layer1"] = scene.tilemap.layer1;
            tm["layer2"] = scene.tilemap.layer2;
            tm["chunkSize"] = scene.tilemap.chunkSize;
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
            if (item.contains("physics")) {
                const auto& a=item.at("physics"); auto& b=o.physics;
                b.enabled=a.value("enabled",false);
                b.type=static_cast<BodyType>(std::clamp(a.value("type",1),0,2));
                if(a.contains("size")) b.size={a["size"][0].get<float>(),a["size"][1].get<float>()};
                b.mass=std::max(.0001f,a.value("mass",1.f));
                if(a.contains("velocity")) b.velocity={a["velocity"][0].get<float>(),a["velocity"][1].get<float>()};
                if(a.contains("acceleration")) b.acceleration={a["acceleration"][0].get<float>(),a["acceleration"][1].get<float>()};
                if(a.contains("force")) b.force={a["force"][0].get<float>(),a["force"][1].get<float>()};
                b.torque=a.value("torque",0.f);
                b.gravityScale=a.value("gravityScale",1.f); b.linearDrag=a.value("linearDrag",0.f);
                b.angularDrag=a.value("angularDrag",0.f); b.friction=a.value("friction",.6f);
                b.restitution=a.value("restitution",0.f);b.inertia=std::max(.0001f,a.value("inertia",1.f));
                b.angularVelocity=a.value("angularVelocity",0.f);
                b.angularAcceleration=a.value("angularAcceleration",0.f);
                b.collideWithTiles=a.value("collideWithTiles",true);
                b.collideWithBodies=a.value("collideWithBodies",true);
                b.rotatingFrameRadiansPerSecond=a.value("rotatingFrameRadiansPerSecond",0.f);
                if(a.contains("rotatingFrameOrigin")) b.rotatingFrameOrigin={a["rotatingFrameOrigin"][0].get<float>(),a["rotatingFrameOrigin"][1].get<float>()};
            }
            if (o.id <= 0 || o.id > 10000000) throw std::runtime_error("Invalid object ID");
            if (loaded.find(o.id)) throw std::runtime_error("Duplicate object ID");
            loaded.nextId = std::max(loaded.nextId, o.id + 1);
            loaded.objects.push_back(o);
            if (loaded.objects.size() > 10000) throw std::runtime_error("Too many objects");
        }
        if (j.contains("physicsSettings")) {
            const auto& a=j.at("physicsSettings");
            if(a.contains("gravity")) loaded.physics.gravity={a["gravity"][0].get<float>(),a["gravity"][1].get<float>()};
            loaded.physics.fixedHz=std::clamp(a.value("fixedHz",240),30,480);
            loaded.physics.maxRenderFps=std::clamp(a.value("maxRenderFps",480),30,1000);
        }
        if (j.contains("textObjects")) for(const auto& a:j.at("textObjects")) {
            TextObject t; t.id=a.value("id",0);t.name=a.value("name",std::string{"Text"});
            t.text=a.value("text",std::string{});t.bitmapFont=a.value("bitmapFont",std::string{});
            if(!t.bitmapFont.empty()) validateAssetPath(t.bitmapFont);
            if(a.contains("position")) t.position={a["position"][0].get<float>(),a["position"][1].get<float>()};
            t.pixelScale=a.value("pixelScale",2.f);
            if(a.contains("color")) t.color=sf::Color(a["color"][0].get<std::uint8_t>(),a["color"][1].get<std::uint8_t>(),a["color"][2].get<std::uint8_t>(),a["color"][3].get<std::uint8_t>());
            if(t.id<=0 || loaded.find(t.id) || loaded.findText(t.id)) throw std::runtime_error("Invalid text object ID");
            loaded.nextId=std::max(loaded.nextId,t.id+1);
            loaded.textObjects.push_back(std::move(t));
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
            loaded.tilemap.layer1=tm.value("layer1",std::vector<int>(required,0));
            loaded.tilemap.layer2=tm.value("layer2",std::vector<int>(required,0));
            if(loaded.tilemap.layer1.size()!=required)loaded.tilemap.layer1.assign(required,0);
            if(loaded.tilemap.layer2.size()!=required)loaded.tilemap.layer2.assign(required,0);
            loaded.tilemap.chunkSize=std::clamp(tm.value("chunkSize",64),8,256);
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

namespace {
std::map<std::string, BitmapFont>& bitmapFontCache() {
    static std::map<std::string, BitmapFont> cache;
    return cache;
}
}
void clearLoadedBitmapFonts() { bitmapFontCache().clear(); }

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
        // Merge all visible tiles sharing a texture into one vertex batch.
        // This avoids thousands of draw calls and is independent of chunk loading.
        for(int layer=0;layer<3;++layer) {
        std::map<const sf::Texture*, sf::VertexArray> batches;
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                const int id = scene.tilemap.getLayer(layer, x, y);
                if (id == 0) continue;
                const TileDefinition* def = scene.tilemap.tileById(id);
                if (!def || def->texture.empty()) continue;
                const sf::Texture* tex = textures.get(root / def->texture);
                if (!tex) continue;
                auto it = batches.find(tex);
                if (it == batches.end())
                    it = batches.emplace(tex, sf::VertexArray(sf::PrimitiveType::Triangles)).first;
                auto& arr = it->second;
                const auto ts = tex->getSize();
                const float bx = static_cast<float>(x * scene.tilemap.blockSize);
                const float by = static_cast<float>(y * scene.tilemap.blockSize);
                const float size = static_cast<float>(scene.tilemap.blockSize);
                const float tx = static_cast<float>(ts.x), ty = static_cast<float>(ts.y);
                arr.append(sf::Vertex{{bx,by},sf::Color::White,{0,0}});
                arr.append(sf::Vertex{{bx+size,by},sf::Color::White,{tx,0}});
                arr.append(sf::Vertex{{bx,by+size},sf::Color::White,{0,ty}});
                arr.append(sf::Vertex{{bx,by+size},sf::Color::White,{0,ty}});
                arr.append(sf::Vertex{{bx+size,by},sf::Color::White,{tx,0}});
                arr.append(sf::Vertex{{bx+size,by+size},sf::Color::White,{tx,ty}});
            }
        }
        for (const auto& batch : batches) target.draw(batch.second, batch.first);
        } // layer
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
    // Render bitmap-font text objects. Font files are loaded on demand. A small
    // file cache avoids reparsing JSON on every frame; editor refresh clears it.
    auto& fontCache = bitmapFontCache();
    for (const auto& text : scene.textObjects) {
        if (text.bitmapFont.empty()) continue;
        const std::string key=(root / text.bitmapFont).string();
        auto it=fontCache.find(key);
        if(it==fontCache.end()) {
            BitmapFont loaded; loaded.reset(8,8);
            std::string err;
            if(!loadBitmapFont(loaded,root/text.bitmapFont,err)) continue;
            it=fontCache.emplace(key,std::move(loaded)).first;
        }
        drawBitmapText(target,it->second,text.text,text.position,text.pixelScale,text.color);
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
