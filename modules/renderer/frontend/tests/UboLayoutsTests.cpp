#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sonnet/renderer/frontend/UboLayouts.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace sonnet::renderer::frontend;
using sonnet::api::render::DirectionalLight;
using sonnet::api::render::FrameContext;
using sonnet::api::render::PointLight;
using Catch::Matchers::WithinAbs;

namespace {

FrameContext makeCtx(const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye) {
    return FrameContext{view, proj, eye, 800, 600, 1.0f / 60.0f, std::nullopt, {}, std::nullopt};
}

} // namespace

TEST_CASE("CameraUBO: copies view/projection/position from FrameContext", "[ubo][camera]") {
    const glm::mat4 view = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, -5.0f});
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::vec3 eye{1.0f, 2.0f, 3.0f};
    auto ctx = makeCtx(view, proj, eye);

    auto ubo = buildCameraUBO(ctx);

    REQUIRE(ubo.uView         == view);
    REQUIRE(ubo.uProjection   == proj);
    REQUIRE(ubo.uViewPosition == eye);
}

TEST_CASE("CameraUBO: inverse matrices satisfy A * inv(A) ~= I", "[ubo][camera]") {
    const glm::mat4 view = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, -5.0f});
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    auto ctx = makeCtx(view, proj, glm::vec3{0.0f});

    auto ubo = buildCameraUBO(ctx);

    const glm::mat4 ident = (proj * view) * ubo.uInvViewProj;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            const float expected = (c == r) ? 1.0f : 0.0f;
            REQUIRE_THAT(ident[c][r], WithinAbs(expected, 1e-3f));
        }
    }

    const glm::mat4 ident2 = proj * ubo.uInvProjection;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            const float expected = (c == r) ? 1.0f : 0.0f;
            REQUIRE_THAT(ident2[c][r], WithinAbs(expected, 1e-3f));
        }
    }
}

TEST_CASE("LightsUBO: empty FrameContext yields zero point lights and default DirLight",
         "[ubo][lights]") {
    glm::mat4 v{1.0f}, p{1.0f}; glm::vec3 e{0.0f};
    auto ctx = makeCtx(v, p, e);

    auto ubo = buildLightsUBO(ctx);

    REQUIRE(ubo.uPointLightCount == 0);
    REQUIRE(ubo.uDirLight.direction == glm::vec3{0.0f});
    REQUIRE(ubo.uDirLight.color     == glm::vec3{0.0f});
}

TEST_CASE("LightsUBO: directional light fields propagate", "[ubo][lights]") {
    glm::mat4 v{1.0f}, p{1.0f}; glm::vec3 e{0.0f};
    auto ctx = makeCtx(v, p, e);
    DirectionalLight dl;
    dl.direction = glm::vec3{0.0f, -1.0f, 0.0f};
    dl.color     = glm::vec3{1.0f, 0.5f, 0.25f};
    dl.intensity = 2.5f;
    ctx.directionalLight = dl;

    auto ubo = buildLightsUBO(ctx);
    REQUIRE(ubo.uDirLight.direction == dl.direction);
    REQUIRE(ubo.uDirLight.color     == dl.color);
    REQUIRE_THAT(ubo.uDirLight.intensity, WithinAbs(2.5f, 1e-6f));
}

TEST_CASE("LightsUBO: point lights up to kMax are copied verbatim", "[ubo][lights]") {
    glm::mat4 v{1.0f}, p{1.0f}; glm::vec3 e{0.0f};
    auto ctx = makeCtx(v, p, e);

    PointLight pl;
    pl.position  = glm::vec3{1.0f, 2.0f, 3.0f};
    pl.color     = glm::vec3{0.5f, 0.5f, 1.0f};
    pl.intensity = 4.0f;
    pl.constant  = 1.0f;
    pl.linear    = 0.5f;
    pl.quadratic = 0.25f;
    ctx.pointLights.push_back(pl);
    ctx.pointLights.push_back(pl);

    auto ubo = buildLightsUBO(ctx);
    REQUIRE(ubo.uPointLightCount == 2);
    REQUIRE(ubo.uPointLights[0].position == pl.position);
    REQUIRE(ubo.uPointLights[1].quadratic == pl.quadratic);
}

TEST_CASE("LightsUBO: clamps to kMaxPointLightsUBO entries", "[ubo][lights]") {
    glm::mat4 v{1.0f}, p{1.0f}; glm::vec3 e{0.0f};
    auto ctx = makeCtx(v, p, e);

    for (int i = 0; i < kMaxPointLightsUBO + 4; ++i) {
        ctx.pointLights.push_back(PointLight{});
    }

    auto ubo = buildLightsUBO(ctx);
    REQUIRE(ubo.uPointLightCount == kMaxPointLightsUBO);
}
