#pragma once
#include <SFML/System/Vector2.hpp>
#include <cstdint>

namespace forge {
struct Scene;

enum class BodyType : int { Static = 0, Dynamic = 1, Kinematic = 2 };

// Distances use scene pixels; time uses seconds; force uses mass * pixels / s^2.
// Y increases downwards in SFML. Rotation and angular velocity are in degrees.
struct PhysicsBody {
    bool enabled = false;
    BodyType type = BodyType::Dynamic;
    bool collideWithTiles = true;
    bool collideWithBodies = true;
    sf::Vector2f size{32.f, 32.f}; // Axis-aligned collider size, NOT sprite size.
    float mass = 1.f;
    sf::Vector2f velocity{};
    sf::Vector2f acceleration{}; // externally chosen acceleration, e.g. thrust
    sf::Vector2f force{};        // accumulated force; reset after every fixed step
    float angularVelocity = 0.f;
    float angularAcceleration = 0.f;
    float torque = 0.f;
    float inertia = 1.f;
    float linearDrag = 0.f;
    float angularDrag = 0.f;
    float friction = 0.6f;
    float restitution = 0.f;
    float gravityScale = 1.f;
    // Optional rotating-reference-frame centrifugal acceleration omega^2 * r.
    // This is a pseudoforce, not an automatically generated force for turns.
    float rotatingFrameRadiansPerSecond = 0.f;
    sf::Vector2f rotatingFrameOrigin{};
};

struct PhysicsSettings {
    sf::Vector2f gravity{0.f, 980.f};  // Approx. 100 scene pixels per meter.
    int fixedHz = 240;
    int maxRenderFps = 480; // best effort, not a guarantee
};

void applyForce(PhysicsBody& body, sf::Vector2f force);
void applyImpulse(PhysicsBody& body, sf::Vector2f impulse);
void applyTorque(PhysicsBody& body, float torque);
void stepPhysics(Scene& scene, float fixedDt);
} // namespace forge
