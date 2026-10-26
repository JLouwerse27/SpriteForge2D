#pragma once
#include <SFML/Graphics.hpp>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace forge {
namespace fs = std::filesystem;

struct Object {
    int id = 0;
    std::string name = "Sprite";
    std::string texture; // relative to the scene directory (e.g., assets/hero.png)
    sf::Vector2f position{0.f, 0.f};
    sf::Vector2f scale{1.f, 1.f};
    float rotation = 0.f; // degrees
    bool visible = true;
    bool controllable = false;
    float moveSpeed = 230.f;
    // Name of a statically compiled C++ behavior. Empty = no custom behavior.
    std::string behavior;
    std::map<std::string, float> behaviorValues;
};

struct Scene {
    std::string title = "My Game";
    unsigned width = 960;
    unsigned height = 540;
    sf::Color background{38, 43, 59};
    std::vector<Object> objects;
    int nextId = 1;

    Object& add(const std::string& name, const std::string& texture, sf::Vector2f position);
    Object* find(int id);
    const Object* find(int id) const;
    bool remove(int id);
};

class TextureCache {
public:
    const sf::Texture* get(const fs::path& absolutePath);
    void invalidate(const fs::path& absolutePath);
    void clear();
private:
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> textures_;
};

bool saveScene(const Scene& scene, const fs::path& path, std::string& error);
bool loadScene(Scene& scene, const fs::path& path, std::string& error);

// Draw in scene units. Caller chooses the target's view.
void drawScene(sf::RenderTarget& target, const Scene& scene,
               const fs::path& projectRoot, TextureCache& textures,
               int selectedId = -1, bool drawGrid = false, float pixelScale = 1.f);

// Screen/world hit testing respects sprite scale and rotation.
int findObjectAt(const Scene& scene, sf::Vector2f point,
                 const fs::path& projectRoot, TextureCache& textures);

// A basic game behavior for the first engine version: WASD / arrows.
void updatePlayerControls(Scene& scene, float dt, bool left, bool right, bool up, bool down);

// Returns absolute executable folder, independent of working directory.
fs::path executableDirectory();
} // namespace forge
