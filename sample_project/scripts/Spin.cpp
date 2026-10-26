#include "Behaviors.h"
#include <cmath>

class Spin final : public forge::Behavior {
public:
    void onUpdate(forge::BehaviorContext& ctx) override {
        ctx.object.rotation = std::fmod(
            ctx.object.rotation + ctx.setting("DegreesPerSecond", 45.f) * ctx.deltaTime, 360.f);
    }
};

void register_Spin(forge::BehaviorRegistry& registry, const std::string& description,
                   const std::vector<forge::FloatSetting>& settings) {
    registry.add<Spin>("Spin", description, settings);
}
