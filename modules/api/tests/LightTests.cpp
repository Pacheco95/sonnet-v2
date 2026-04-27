#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sonnet/api/render/Light.h>

using namespace sonnet::api::render;
using Catch::Matchers::WithinAbs;

TEST_CASE("DirectionalLight: defaults match documented values", "[light]") {
    DirectionalLight l{};
    REQUIRE(l.direction == glm::vec3{0.0f, -1.0f, 0.0f});
    REQUIRE(l.color     == glm::vec3{1.0f, 1.0f, 1.0f});
    REQUIRE_THAT(l.intensity, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("PointLight: defaults match documented attenuation values", "[light]") {
    PointLight l{};
    REQUIRE(l.position == glm::vec3{0.0f});
    REQUIRE(l.color    == glm::vec3{1.0f, 1.0f, 1.0f});
    REQUIRE_THAT(l.intensity, WithinAbs(1.0f,   1e-6f));
    REQUIRE_THAT(l.constant,  WithinAbs(1.0f,   1e-6f));
    REQUIRE_THAT(l.linear,    WithinAbs(0.09f,  1e-6f));
    REQUIRE_THAT(l.quadratic, WithinAbs(0.032f, 1e-6f));
}
