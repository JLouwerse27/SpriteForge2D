#include "Behaviors.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace forge {

InputState readKeyboardInput() {
    using Key = sf::Keyboard::Key;
    return {
        sf::Keyboard::isKeyPressed(Key::A) || sf::Keyboard::isKeyPressed(Key::Left),
        sf::Keyboard::isKeyPressed(Key::D) || sf::Keyboard::isKeyPressed(Key::Right),
        sf::Keyboard::isKeyPressed(Key::W) || sf::Keyboard::isKeyPressed(Key::Up),
        sf::Keyboard::isKeyPressed(Key::S) || sf::Keyboard::isKeyPressed(Key::Down),
        sf::Keyboard::isKeyPressed(Key::Space)
    };
}

BehaviorContext::BehaviorContext(Scene& s, Object& o, const InputState& i, float dt,
                                 std::vector<Object>& spawns, std::vector<int>& removals)
    : scene(s), object(o), input(i), deltaTime(dt), spawns_(spawns), removals_(removals) {}

float BehaviorContext::setting(const std::string& name, float fallback) const {
    const auto it = object.behaviorValues.find(name);
    return it == object.behaviorValues.end() ? fallback : it->second;
}

int BehaviorContext::spawnSprite(std::string name, std::string texture, sf::Vector2f position,
                                 std::string behavior, std::map<std::string, float> settings) {
    Object obj;
    obj.id = scene.nextId++;
    obj.name = std::move(name);
    obj.texture = std::move(texture);
    obj.position = position;
    obj.behavior = std::move(behavior);
    obj.behaviorValues = std::move(settings);
    const int id = obj.id;
    spawns_.push_back(std::move(obj));
    return id;
}

void BehaviorContext::destroy(int id) { removals_.push_back(id); }

const BehaviorDefinition* BehaviorRegistry::find(const std::string& name) const {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(),
        [&](const BehaviorDefinition& def) { return def.name == name; });
    return it == definitions_.end() ? nullptr : &*it;
}

void validateBehaviors(const Scene& scene, const BehaviorRegistry& registry) {
    for (const auto& obj : scene.objects) {
        if (!obj.behavior.empty() && !registry.find(obj.behavior))
            throw std::runtime_error("Object '" + obj.name + "' references missing C++ behavior '" +
                                     obj.behavior + "'. Register and rebuild your C++ game code.");
    }
}

void BehaviorSystem::attach(Scene& scene, Object& obj) {
    if (obj.behavior.empty() || instances_.count(obj.id)) return;
    const auto* def = registry_.find(obj.behavior);
    if (!def) throw std::runtime_error("Unknown C++ behavior: " + obj.behavior);
    auto instance = def->create();
    if (!instance) throw std::runtime_error("Could not construct behavior: " + obj.behavior);
    auto [it, inserted] = instances_.emplace(obj.id, Instance{obj.behavior, std::move(instance)});
    (void)inserted;
    BehaviorContext ctx(scene, obj, input_, 0.f, pendingSpawns_, pendingRemovals_);
    it->second.script->onCreate(ctx);
}

void BehaviorSystem::flush(Scene& scene) {
    // Process destruction first. onDestroy() is invoked while objects still exist.
    const auto removals = std::move(pendingRemovals_);
    pendingRemovals_.clear();
    std::unordered_set<int> removeSet(removals.begin(), removals.end());
    for (int id : removeSet) {
        auto instance = instances_.find(id);
        if (instance != instances_.end()) {
            if (auto* obj = scene.find(id)) {
                BehaviorContext ctx(scene, *obj, input_, 0.f, pendingSpawns_, pendingRemovals_);
                instance->second.script->onDestroy(ctx);
            }
            instances_.erase(instance);
        }
        scene.remove(id);
    }
    // Newly spawned objects start executing on the next tick.
    for (auto& obj : pendingSpawns_) {
        if (!removeSet.count(obj.id)) scene.objects.push_back(std::move(obj));
    }
    pendingSpawns_.clear();
}

void BehaviorSystem::start(Scene& scene) {
    if (started_) stop(scene);
    validateBehaviors(scene, registry_);
    started_ = true;
    try {
        const std::size_t originalCount = scene.objects.size();
        for (std::size_t i = 0; i < originalCount; ++i) attach(scene, scene.objects[i]);
        flush(scene);
    } catch (...) {
        stop(scene);
        throw;
    }
}

void BehaviorSystem::update(Scene& scene, const InputState& input, float dt) {
    if (!started_) start(scene);
    input_ = input;
    dt = std::clamp(dt, 0.f, 0.05f);

    // Snapshot IDs rather than iterating references that could be invalidated.
    std::vector<int> ids;
    ids.reserve(scene.objects.size());
    for (const auto& obj : scene.objects) ids.push_back(obj.id);
    for (int id : ids) {
        Object* obj = scene.find(id);
        if (!obj) continue;
        if (obj->behavior.empty()) continue;
        if (!instances_.count(id)) attach(scene, *obj);
        obj = scene.find(id);
        if (obj) {
            BehaviorContext ctx(scene, *obj, input_, dt, pendingSpawns_, pendingRemovals_);
            instances_.at(id).script->onUpdate(ctx);
        }
    }
    // Preserve compatibility with existing SpriteForge scenes that used the
    // original Controllable checkbox instead of a C++ behavior.
    updatePlayerControls(scene, dt, input.left, input.right, input.up, input.down);
    flush(scene);
}

void BehaviorSystem::stop(Scene& scene) {
    for (auto& pair : instances_) {
        if (auto* obj = scene.find(pair.first)) {
            BehaviorContext ctx(scene, *obj, input_, 0.f, pendingSpawns_, pendingRemovals_);
            pair.second.script->onDestroy(ctx);
        }
    }
    instances_.clear();
    pendingSpawns_.clear();
    pendingRemovals_.clear();
    started_ = false;
}

} // namespace forge
