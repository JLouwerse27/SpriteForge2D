#include "GameBehaviors.h"
#include "Behaviors.h"
#include <algorithm>
#include <cmath>

using namespace forge;

// Example 1: move a sprite with WASD/arrow keys.
// Select MoveWithKeys in the SpriteForge Inspector, then adjust Speed.
class MoveWithKeys final : public Behavior {
public:
    void onUpdate(BehaviorContext& ctx) override {
        float dx = static_cast<float>(ctx.input.right) - static_cast<float>(ctx.input.left);
        float dy = static_cast<float>(ctx.input.down) - static_cast<float>(ctx.input.up);
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.f) { dx /= length; dy /= length; }
        const float speed = std::max(0.f, ctx.setting("Speed", 230.f));
        ctx.object.position.x = std::clamp(ctx.object.position.x + dx * speed * ctx.deltaTime,
                                          0.f, static_cast<float>(ctx.scene.width));
        ctx.object.position.y = std::clamp(ctx.object.position.y + dy * speed * ctx.deltaTime,
                                          0.f, static_cast<float>(ctx.scene.height));
    }
};

// Example 2: rotate automatically. Can be attached to gears or radar dishes.
class Spin final : public Behavior {
public:
    void onUpdate(BehaviorContext& ctx) override {
        ctx.object.rotation = std::fmod(
            ctx.object.rotation + ctx.setting("DegreesPerSecond", 45.f) * ctx.deltaTime, 360.f);
    }
};

// Example 3: oscillate horizontally between two offsets around spawn position.
// Demonstrates onCreate() and per-instance state that isn't written to scene.json.
class Patrol final : public Behavior {
    float startX_ = 0.f;
    float elapsed_ = 0.f;
public:
    void onCreate(BehaviorContext& ctx) override { startX_ = ctx.object.position.x; }
    void onUpdate(BehaviorContext& ctx) override {
        elapsed_ += ctx.deltaTime;
        const float amplitude = std::max(0.f, ctx.setting("Distance", 100.f));
        const float speed = ctx.setting("CyclesPerSecond", 0.5f);
        constexpr float tau = 6.28318530718f;
        ctx.object.position.x = startX_ + amplitude * std::sin(elapsed_ * tau * speed);
    }
};

void registerGameBehaviors(BehaviorRegistry& registry) {
    registry.add<MoveWithKeys>("MoveWithKeys", "Move with WASD or the arrow keys",
        {{"Speed", 230.f, 0.f, 3000.f, 1.f}});
    registry.add<Spin>("Spin", "Rotate the sprite each frame",
        {{"DegreesPerSecond", 45.f, -720.f, 720.f, 1.f}});
    registry.add<Patrol>("Patrol", "Move left/right around the starting position",
        {{"Distance", 100.f, 0.f, 3000.f, 1.f},
         {"CyclesPerSecond", 0.5f, -10.f, 10.f, 0.05f}});
}
