#define GLM_ENABLE_EXPERIMENTAL
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sonnet/world/CameraComponent.h>
#include <sonnet/world/Transform.h>

using sonnet::world::CameraComponent;
using sonnet::world::Transform;

TEST_CASE("CameraComponent: viewMatrix places world origin behind camera", "[camera]") {
    Transform t;
    t.setLocalPosition({0.0f, 0.0f, 5.0f}); // camera 5 units along +Z
    CameraComponent cam;

    const glm::mat4 view   = cam.viewMatrix(t);
    const glm::vec4 origin = view * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};

    // In view space the world origin should be 5 units in front of the camera,
    // which in right-handed OpenGL is z = -5.
    REQUIRE_THAT(origin.z, Catch::Matchers::WithinAbs(-5.0f, 1e-4f));
}

TEST_CASE("CameraComponent: projectionMatrix x-scale matches 1/aspect", "[camera]") {
    // For fov=90° vertical, tan(45°)=1, so x-scale = 1 / (aspect * tan(fov/2)) = 1/aspect.
    CameraComponent cam{.fov = 90.0f, .near = 0.1f, .far = 100.0f};
    constexpr float aspect = 2.0f;
    const glm::mat4 proj = cam.projectionMatrix(aspect);

    REQUIRE_THAT(proj[0][0], Catch::Matchers::WithinAbs(1.0f / aspect, 1e-4f));
}
