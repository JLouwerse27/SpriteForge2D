#include "Behaviors.h"
#include <algorithm>
#include <cmath>

class MoveWithKeys final : public forge::Behavior {
public:
    void onUpdate(forge::BehaviorContext& ctx) override {
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

void register_MoveWithKeys(forge::BehaviorRegistry& registry, const std::string& description,
                           const std::vector<forge::FloatSetting>& settings) {
    registry.add<MoveWithKeys>("MoveWithKeys", description, settings);
}
