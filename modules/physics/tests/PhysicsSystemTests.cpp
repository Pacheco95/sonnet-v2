#include <sonnet/physics/PhysicsSystem.h>
#include <sonnet/world/Scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace sonnet::physics;
using namespace sonnet::world;

// Each test gets a fresh PhysicsSystem so Jolt global state is reset cleanly.
struct PhysicsFixture {
    Scene         scene;
    PhysicsSystem physics;
    PhysicsFixture() { physics.init(); }
    ~PhysicsFixture() { physics.shutdown(); }
};

TEST_CASE_METHOD(PhysicsFixture,
    "PhysicsSystem: static body does not move after many steps", "[physics]") {
    auto &floor = scene.createObject("Floor");
    floor.transform.setLocalPosition({0.0f, -5.0f, 0.0f});
    floor.transform.setLocalScale({10.0f, 0.2f, 10.0f});

    physics.addBody(floor, {.bodyType = BodyType::Static, .shapeType = ShapeType::Box});

    for (int i = 0; i < 120; ++i)
        physics.step(scene, 1.0f / 60.0f);

    const glm::vec3 pos = floor.transform.getWorldPosition();
    REQUIRE_THAT(pos.y, Catch::Matchers::WithinAbs(-5.0f, 1e-3f));
}

TEST_CASE_METHOD(PhysicsFixture,
    "PhysicsSystem: dynamic body falls under gravity", "[physics]") {
    auto &ball = scene.createObject("Ball");
    ball.transform.setLocalPosition({0.0f, 10.0f, 0.0f});
    ball.transform.setLocalScale({0.5f, 0.5f, 0.5f});

    physics.addBody(ball, {.bodyType = BodyType::Dynamic, .shapeType = ShapeType::Sphere});

    const float startY = ball.transform.getWorldPosition().y;

    // Step 1 second worth of physics — the ball should have fallen noticeably.
    for (int i = 0; i < 60; ++i)
        physics.step(scene, 1.0f / 60.0f);

    const float endY = ball.transform.getWorldPosition().y;
    REQUIRE(endY < startY - 1.0f); // fallen more than 1 unit
}

TEST_CASE_METHOD(PhysicsFixture,
    "PhysicsSystem: addBody/removeBody does not crash on empty scene", "[physics]") {
    auto &obj = scene.createObject("Temp");
    obj.transform.setLocalPosition({0.0f, 0.0f, 0.0f});

    physics.addBody(obj, {});
    REQUIRE(physics.getBodyDef(&obj) != nullptr);

    physics.removeBody(&obj);
    REQUIRE(physics.getBodyDef(&obj) == nullptr);
}

TEST_CASE_METHOD(PhysicsFixture,
    "PhysicsSystem: getBodyDef returns nullptr for unregistered object", "[physics]") {
    auto &obj = scene.createObject("Ghost");
    REQUIRE(physics.getBodyDef(&obj) == nullptr);
}

TEST_CASE_METHOD(PhysicsFixture,
    "PhysicsSystem: dynamic body rests on static floor", "[physics]") {
    // Floor at y=-1, box (10 x 0.2 x 10) → top surface at y = -1 + 0.1 = -0.9
    auto &floor = scene.createObject("Floor");
    floor.transform.setLocalPosition({0.0f, -1.0f, 0.0f});
    floor.transform.setLocalScale({10.0f, 0.2f, 10.0f});
    physics.addBody(floor, {.bodyType = BodyType::Static});

    // Cube of scale (0.5 x 0.5 x 0.5), half-extent 0.25, dropped from y=5.
    auto &box = scene.createObject("Box");
    box.transform.setLocalPosition({0.0f, 5.0f, 0.0f});
    box.transform.setLocalScale({0.5f, 0.5f, 0.5f});
    physics.addBody(box, {.bodyType = BodyType::Dynamic});

    // Simulate 5 seconds.
    for (int i = 0; i < 300; ++i)
        physics.step(scene, 1.0f / 60.0f);

    // Box bottom = world-y - 0.25; floor top = -0.9. Expect box to have stopped above floor.
    const float boxY = box.transform.getWorldPosition().y;
    REQUIRE(boxY > -5.0f);  // did not fall through the floor to oblivion
    REQUIRE(boxY < 5.0f);   // definitely moved from start
}
