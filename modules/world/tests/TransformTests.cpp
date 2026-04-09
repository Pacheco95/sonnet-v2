#define GLM_ENABLE_EXPERIMENTAL
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <sonnet/world/Transform.h>

using sonnet::world::Transform;

namespace {
constexpr float kEps = 1e-4f;

void requireNear(const glm::vec3 &actual, const glm::vec3 &expected, float eps = kEps) {
    INFO("actual   = (" << actual.x << ", " << actual.y << ", " << actual.z << ")");
    INFO("expected = (" << expected.x << ", " << expected.y << ", " << expected.z << ")");
    REQUIRE(glm::all(glm::epsilonEqual(actual, expected, eps)));
}

void requireNear(const glm::quat &actual, const glm::quat &expected, float eps = kEps) {
    const glm::quat a = glm::normalize(actual);
    const glm::quat e = glm::normalize(expected);
    const float d = std::min(glm::length(a - e), glm::length(a + e));
    REQUIRE(d <= eps);
}
} // namespace

// ── Local position ────────────────────────────────────────────────────────────

TEST_CASE("Transform: local position updates model matrix translation", "[world]") {
    Transform t;
    t.setLocalPosition({1.0f, 2.0f, 3.0f});

    requireNear(t.getLocalPosition(), {1.0f, 2.0f, 3.0f});
    // Translation is stored in column 3 of the column-major matrix.
    requireNear(glm::vec3(t.getModelMatrix()[3]), {1.0f, 2.0f, 3.0f});
    requireNear(t.getWorldPosition(), {1.0f, 2.0f, 3.0f});
}

// ── Parent-child position ─────────────────────────────────────────────────────

TEST_CASE("Transform: parented world position equals parent + local", "[world]") {
    Transform parent, child;
    parent.setLocalPosition({10.0f, 0.0f, 0.0f});
    child.setLocalPosition({ 1.0f, 0.0f, 0.0f});
    child.setParent(&parent, /*keepWorldTransform=*/false);

    requireNear(child.getLocalPosition(), {1.0f, 0.0f, 0.0f});
    requireNear(child.getWorldPosition(), {11.0f, 0.0f, 0.0f});
}

TEST_CASE("Transform: setWorldPosition with parent computes correct local position", "[world]") {
    Transform parent, child;
    parent.setLocalPosition({10.0f, 0.0f, 0.0f});
    child.setParent(&parent, false);
    child.setWorldPosition({11.0f, 0.0f, 0.0f});

    requireNear(child.getLocalPosition(), {1.0f, 0.0f, 0.0f});
    requireNear(child.getWorldPosition(), {11.0f, 0.0f, 0.0f});
}

TEST_CASE("Transform: setParent(keepWorld=true) preserves world position", "[world]") {
    Transform parent, child;
    parent.setLocalPosition({10.0f, 0.0f, 0.0f});
    child.setLocalPosition({ 2.0f, 0.0f, 0.0f});

    const glm::vec3 originalWorld = child.getWorldPosition();

    child.setParent(&parent, /*keepWorldTransform=*/true);

    requireNear(child.getWorldPosition(), originalWorld);
    requireNear(child.getLocalPosition(), {-8.0f, 0.0f, 0.0f});
}

// ── Rotation ──────────────────────────────────────────────────────────────────

TEST_CASE("Transform: world rotation composes parent and local", "[world]") {
    Transform parent, child;
    const glm::quat parentRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    const glm::quat childRot  = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));

    parent.setLocalRotation(parentRot);
    child.setLocalRotation(childRot);
    child.setParent(&parent, false);

    requireNear(child.getWorldRotation(), parentRot * childRot);
}

TEST_CASE("Transform: setWorldRotation with parent adjusts local rotation", "[world]") {
    Transform parent, child;
    const glm::quat parentRot   = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
    const glm::quat desiredWorld= glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 0, 1));

    parent.setLocalRotation(parentRot);
    child.setParent(&parent, false);
    child.setWorldRotation(desiredWorld);

    requireNear(child.getLocalRotation(), glm::inverse(parentRot) * desiredWorld);
    requireNear(child.getWorldRotation(), desiredWorld);
}

// ── Mutation helpers ──────────────────────────────────────────────────────────

TEST_CASE("Transform: translate increments local position", "[world]") {
    Transform t;
    t.setLocalPosition({1.0f, 2.0f, 3.0f});
    t.translate({4.0f, 5.0f, 6.0f});
    requireNear(t.getLocalPosition(), {5.0f, 7.0f, 9.0f});
}

TEST_CASE("Transform: rotate(axis, angleDeg) changes local rotation", "[world]") {
    Transform t;
    t.setLocalRotation(glm::quat(1, 0, 0, 0));
    t.rotate(glm::vec3(0, 1, 0), 90.0f);

    const glm::quat expected = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    requireNear(t.getLocalRotation(), expected);
}

TEST_CASE("Transform: default scale is (1,1,1)", "[world]") {
    Transform t;
    requireNear(t.getLocalScale(), {1.0f, 1.0f, 1.0f});
}
