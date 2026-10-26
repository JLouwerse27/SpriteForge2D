#include "Behaviors.h"
#include <cmath>
#include <algorithm>

class Patrol final : public forge::Behavior {
    float startX_ = 0.f;
    float elapsed_ = 0.f;
public:
    void onCreate(forge::BehaviorContext& ctx) override { startX_ = ctx.object.position.x; }
    void onUpdate(forge::BehaviorContext& ctx) override {
        elapsed_ += ctx.deltaTime;
        const float amplitude = std::max(0.f, ctx.setting("Distance", 100.f));
        const float speed = ctx.setting("CyclesPerSecond", 0.5f);
        constexpr float tau = 6.28318530718f;
        ctx.object.position.x = startX_ + amplitude * std::sin(elapsed_ * tau * speed);
    }
};

void register_Patrol(forge::BehaviorRegistry& registry, const std::string& description,
                     const std::vector<forge::FloatSetting>& settings) {
    registry.add<Patrol>("Patrol", description, settings);
}
