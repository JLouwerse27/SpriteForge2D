#include "Physics.h"
#include "EngineCore.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace forge {
namespace {
float clamp01(float f) { return std::clamp(f, 0.f, 1.f); }
struct Box { float x, y, w, h; };
Box bounds(const Object& o) {
    return {o.position.x, o.position.y,
            std::max(1.f, o.physics.size.x), std::max(1.f, o.physics.size.y)};
}
bool overlap(const Box& a, const Box& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}
bool solidAt(const Scene& s, int x, int y) {
    const auto& tm = s.tilemap;
    if (!tm.enabled || !tm.inBounds(x, y)) return false;
    const int id = tm.get(x, y);
    if (id == 0) return false;
    const TileDefinition* def = tm.tileById(id);
    return def && def->solid;
}
void collideTileAxis(Scene& s, Object& o, bool horizontal) {
    auto& p = o.physics;
    if (!p.collideWithTiles || !s.tilemap.enabled) return;
    const auto& tm = s.tilemap;
    const float tile = static_cast<float>(tm.blockSize);
    if (tile <= 0.f) return;
    Box box = bounds(o);
    int x0 = std::max(0, static_cast<int>(std::floor(box.x/tile)));
    int y0 = std::max(0, static_cast<int>(std::floor(box.y/tile)));
    int x1 = std::min(tm.width-1, static_cast<int>(std::floor((box.x+box.w-0.001f)/tile)));
    int y1 = std::min(tm.height-1, static_cast<int>(std::floor((box.y+box.h-0.001f)/tile)));
    for (int y=y0; y<=y1; ++y) for (int x=x0; x<=x1; ++x) {
        if (!solidAt(s,x,y)) continue;
        Box t{tile*x,tile*y,tile,tile};
        if (!overlap(box,t)) continue;
        if (horizontal) {
            if (p.velocity.x > 0.f) o.position.x = t.x-box.w;
            else if (p.velocity.x < 0.f) o.position.x = t.x+t.w;
            else continue;
            p.velocity.x = -p.velocity.x * clamp01(p.restitution);
            p.velocity.y *= std::max(0.f,1.f-clamp01(p.friction)*0.02f);
        } else {
            if (p.velocity.y > 0.f) o.position.y = t.y-box.h;
            else if (p.velocity.y < 0.f) o.position.y = t.y+t.h;
            else continue;
            p.velocity.y = -p.velocity.y * clamp01(p.restitution);
            p.velocity.x *= std::max(0.f,1.f-clamp01(p.friction)*0.04f);
        }
        box = bounds(o);
    }
}
void collidePair(Object& a, Object& b) {
    auto& pa=a.physics; auto& pb=b.physics;
    if (!pa.enabled || !pb.enabled || !pa.collideWithBodies || !pb.collideWithBodies) return;
    if (pa.type == BodyType::Static && pb.type == BodyType::Static) return;
    const Box aa=bounds(a), bb=bounds(b);
    if (!overlap(aa,bb)) return;
    const float dx1=(aa.x+aa.w)-bb.x, dx2=(bb.x+bb.w)-aa.x;
    const float dy1=(aa.y+aa.h)-bb.y, dy2=(bb.y+bb.h)-aa.y;
    const float px=std::min(dx1,dx2), py=std::min(dy1,dy2);
    const float invA=(pa.type==BodyType::Dynamic)?1.f/std::max(0.0001f,pa.mass):0.f;
    const float invB=(pb.type==BodyType::Dynamic)?1.f/std::max(0.0001f,pb.mass):0.f;
    if (invA+invB <= 0.f) return;
    const sf::Vector2f normal=px<py ? sf::Vector2f{(aa.x+aa.w/2.f < bb.x+bb.w/2.f)?1.f:-1.f,0.f}
                                         : sf::Vector2f{0.f,(aa.y+aa.h/2.f < bb.y+bb.h/2.f)?1.f:-1.f};
    const float penetration=px<py?px:py;
    const float aShare=invA/(invA+invB), bShare=invB/(invA+invB);
    a.position -= normal * (penetration*aShare);
    b.position += normal * (penetration*bShare);
    const sf::Vector2f relative=pb.velocity-pa.velocity;
    const float along=relative.x*normal.x+relative.y*normal.y;
    if (along>=0) return;
    const float restitution=std::min(clamp01(pa.restitution),clamp01(pb.restitution));
    const float impulse=-(1.f+restitution)*along/(invA+invB);
    pa.velocity -= normal*(impulse*invA);
    pb.velocity += normal*(impulse*invB);
}
}

void applyForce(PhysicsBody& body, sf::Vector2f force) { body.force += force; }
void applyImpulse(PhysicsBody& body, sf::Vector2f impulse) {
    if (body.enabled && body.type==BodyType::Dynamic) body.velocity += impulse/std::max(0.0001f,body.mass);
}
void applyTorque(PhysicsBody& body, float torque) { body.torque += torque; }

void stepPhysics(Scene& s, float dt) {
    if (dt<=0.f || dt>0.05f) return;
    for (auto& o:s.objects) {
        auto& p=o.physics;
        if (!p.enabled) continue;
        if (p.type==BodyType::Dynamic) {
            const float invMass=1.f/std::max(0.0001f,p.mass);
            sf::Vector2f accel=s.physics.gravity*p.gravityScale+p.acceleration+p.force*invMass;
            const float omega=p.rotatingFrameRadiansPerSecond;
            const sf::Vector2f r=o.position-p.rotatingFrameOrigin;
            accel += r*(omega*omega); // centrifugal pseudoforce in rotating reference frame
            p.velocity += accel*dt;
            p.velocity *= 1.f/(1.f+std::max(0.f,p.linearDrag)*dt);
            p.angularVelocity += (p.angularAcceleration+p.torque/std::max(0.0001f,p.inertia))*dt;
            p.angularVelocity /= 1.f+std::max(0.f,p.angularDrag)*dt;
        }
        if (p.type!=BodyType::Static) {
            // Separate axes for basic solid tile collision; fast projectiles can tunnel.
            o.position.x += p.velocity.x*dt;
            collideTileAxis(s,o,true);
            o.position.y += p.velocity.y*dt;
            collideTileAxis(s,o,false);
            o.rotation += p.angularVelocity*dt;
        }
        p.force={}; p.torque=0.f;
    }
    for (std::size_t i=0;i<s.objects.size();++i)
        for (std::size_t j=i+1;j<s.objects.size();++j) collidePair(s.objects[i],s.objects[j]);
}
} // namespace forge
