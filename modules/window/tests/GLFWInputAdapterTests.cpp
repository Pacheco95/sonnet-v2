#include <catch2/catch_test_macros.hpp>

#include <sonnet/window/GLFWInputAdapter.h>

using namespace sonnet::window;
using Key = sonnet::api::input::Key;

TEST_CASE("All engine keys are mapped in GLFW_KEY_MAP", "[GLFWInputAdapter]") {
    // Every Key value (except Last, which is a sentinel) must have a GLFW entry.
    REQUIRE(GLFW_KEY_MAP.size() == static_cast<std::size_t>(Key::Last));
}

TEST_CASE("GLFW W key maps to Key::W", "[GLFWInputAdapter]") {
    REQUIRE(GLFW_KEY_MAP.get(GLFW_KEY_W) == Key::W);
}

TEST_CASE("GLFW Escape key maps to Key::Escape", "[GLFWInputAdapter]") {
    REQUIRE(GLFW_KEY_MAP.get(GLFW_KEY_ESCAPE) == Key::Escape);
}

TEST_CASE("Unknown GLFW key returns nullopt", "[GLFWInputAdapter]") {
    REQUIRE(GLFW_KEY_MAP.get(-9999) == std::nullopt);
}
