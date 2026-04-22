#pragma once

#include <sonnet/physics/PhysicsBodyDef.h>

#include <memory>

// Forward declarations to avoid pulling world headers into consumers of this header.
namespace sonnet::world { class Scene; class GameObject; }

namespace sonnet::physics {

// Manages a Jolt Physics simulation.
// Owns a flat map of GameObject* → body, so the world module stays dependency-free.
//
// Typical usage:
//   PhysicsSystem physics;
//   physics.init();
//   physics.addBody(obj, def);   // called from SceneLoader or at runtime
//   // per frame:
//   physics.step(scene, dt);
//   // on shutdown:
//   physics.shutdown();
class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem &)            = delete;
    PhysicsSystem &operator=(const PhysicsSystem &) = delete;

    // Initialise the Jolt library and create the physics world.
    void init();

    // Remove all bodies and shut down the Jolt library.
    void shutdown();

    // Register a physics body for obj using def. The body is positioned from
    // obj.transform world position/rotation; scale is used as box half-extents.
    void addBody(world::GameObject &obj, const PhysicsBodyDef &def);

    // Remove a body from simulation (e.g. when the object is destroyed).
    void removeBody(world::GameObject *obj);

    // Return the body definition for obj, or nullptr if not registered.
    [[nodiscard]] const PhysicsBodyDef *getBodyDef(const world::GameObject *obj) const;

    // Step simulation by dt seconds, then sync body positions/rotations back
    // to their corresponding Transform objects.
    void step(world::Scene &scene, float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sonnet::physics
