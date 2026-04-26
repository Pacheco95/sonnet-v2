# Physics

`modules/physics/` wraps Jolt Physics 5.5 in `PhysicsSystem`, a pimpl-hidden manager with a flat `GameObject* → body` map. The world module stays dependency-free; `physics` knows about `world::GameObject` for transform sync but not vice versa.

## Lifecycle

```cpp
sonnet::physics::PhysicsSystem physics;
physics.init();                                 // initialise Jolt
physics.addBody(obj, def);                      // register a body
// per frame:
physics.step(scene, dt);                        // simulate then sync transforms
// shutdown:
physics.shutdown();                             // release all bodies
```

`init()` constructs Jolt's job system, broadphase, and physics system. `step(scene, dt)` advances the simulation by `dt` seconds, then for every dynamic/kinematic body writes the resulting world position and rotation back to the owning `GameObject`'s `Transform`.

## PhysicsBodyDef

A plain configuration struct passed to `addBody`:

```cpp
struct PhysicsBodyDef {
    BodyType  bodyType       = BodyType::Static;       // Static | Dynamic | Kinematic
    ShapeType shapeType      = ShapeType::Box;         // Box | Sphere
    float     mass           = 1.0f;
    float     friction       = 0.5f;
    float     restitution    = 0.0f;
    float     linearDamping  = 0.05f;
    float     angularDamping = 0.05f;
};
```

`addBody` reads the owning object's transform: world position and rotation set the body pose; for `Box` shapes the transform's local scale is interpreted as half-extents.

`getBodyDef(obj)` returns the original definition (or `nullptr`) — useful for editor UI inspectors.

`removeBody(obj)` removes the body and clears the map entry; call this **before** destroying the `GameObject` so dangling pointers do not enter the simulation.

## Scene integration

`SceneLoader` reads a `physics` JSON object on each scene entry and forwards a `PhysicsBodyDef` to `physics.addBody()`:

```json
{
  "name": "Ball",
  "position": [0, 5, 0],
  "render": { "mesh": "sphere", "material": "lit" },
  "physics": { "bodyType": "dynamic", "shapeType": "sphere", "mass": 1.0 }
}
```

If the loader is given a null `PhysicsSystem*`, the `physics` field is silently ignored — useful for tests and headless tools.
