#pragma once

namespace sonnet::physics {

enum class BodyType  { Static, Dynamic, Kinematic };
enum class ShapeType { Box, Sphere };

// Per-object physics configuration. Stored in PhysicsSystem; not on GameObject.
struct PhysicsBodyDef {
    BodyType  bodyType       = BodyType::Static;
    ShapeType shapeType      = ShapeType::Box;
    float     mass           = 1.0f;
    float     friction       = 0.5f;
    float     restitution    = 0.0f;
    float     linearDamping  = 0.05f;
    float     angularDamping = 0.05f;
};

} // namespace sonnet::physics
