#pragma once
#include "EngineCore.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace forge {

// This contains the input sampled by the active window, not process-global callbacks.
struct InputState {
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool action = false; // Space
};
InputState readKeyboardInput();

struct FloatSetting {
    std::string name;
    float defaultValue = 0.f;
    float minimum = -100000.f;
    float maximum = 100000.f;
    float step = 1.f;
};

class BehaviorContext {
public:
    Scene& scene;
    Object& object;
    const InputState& input;
    float deltaTime;

    float setting(const std::string& name, float fallback = 0.f) const;

    // Changes to the object list are deferred until the end of the tick, so
    // scripts can safely spawn/destroy objects while other scripts update.
    int spawnSprite(std::string name, std::string texture, sf::Vector2f position,
                    std::string behavior = {}, std::map<std::string, float> settings = {});
    void destroy(int objectId);

private:
    friend class BehaviorSystem;
    BehaviorContext(Scene& scene, Object& object, const InputState& input, float dt,
                    std::vector<Object>& spawns, std::vector<int>& removals);
    std::vector<Object>& spawns_;
    std::vector<int>& removals_;
};

class Behavior {
public:
    virtual ~Behavior() = default;
    virtual void onCreate(BehaviorContext&) {}
    virtual void onUpdate(BehaviorContext&) = 0;
    virtual void onDestroy(BehaviorContext&) {}
};

struct BehaviorDefinition {
    std::string name;
    std::string description;
    std::vector<FloatSetting> settings;
    std::function<std::unique_ptr<Behavior>()> create;
};

class BehaviorRegistry {
public:
    template<class T>
    void add(std::string name, std::string description, std::vector<FloatSetting> settings = {}) {
        if (find(name)) throw std::runtime_error("Duplicate behavior: " + name);
        definitions_.push_back({std::move(name), std::move(description),
                                std::move(settings), [] { return std::make_unique<T>(); }});
    }
    void addMetadata(std::string name, std::string description, std::vector<FloatSetting> settings = {});
    const BehaviorDefinition* find(const std::string& name) const;
    const std::vector<BehaviorDefinition>& definitions() const { return definitions_; }

private:
    std::vector<BehaviorDefinition> definitions_;
};

// Instantiates one C++ Behavior per scene Object with a nonempty behavior name.
// The exported GameRuntime links game code; Editor reads behavior metadata only.
class BehaviorSystem {
public:
    explicit BehaviorSystem(const BehaviorRegistry& registry) : registry_(registry) {}
    void start(Scene& scene);
    void update(Scene& scene, const InputState& input, float dt);
    void stop(Scene& scene);
    bool running() const { return started_; }

private:
    struct Instance { std::string name; std::unique_ptr<Behavior> script; };
    void attach(Scene& scene, Object& object);
    void flush(Scene& scene);

    const BehaviorRegistry& registry_;
    std::unordered_map<int, Instance> instances_;
    std::vector<Object> pendingSpawns_;
    std::vector<int> pendingRemovals_;
    InputState input_{};
    bool started_ = false;
};

// Checks the C++ behaviors named in a scene against the behaviors compiled
// into the current executable. Prevents silently exporting missing scripts.
void validateBehaviors(const Scene& scene, const BehaviorRegistry& registry);

} // namespace forge
