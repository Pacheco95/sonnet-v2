#include <catch2/catch_test_macros.hpp>

#include <sonnet/input/InputSystem.h>

using namespace sonnet::input;
using Key         = sonnet::api::input::Key;
using MouseButton = sonnet::api::input::MouseButton;

// ── Key state transitions ─────────────────────────────────────────────────────

TEST_CASE("Key: JustPressed on first press, Held after nextFrame", "[InputSystem]") {
    InputSystem sys;

    sys.onKeyEvent({Key::W, true});

    REQUIRE(sys.isKeyJustPressed(Key::W));
    REQUIRE(sys.isKeyDown(Key::W));
    REQUIRE_FALSE(sys.isKeyJustReleased(Key::W));

    sys.nextFrame();

    // Held — still down but no longer "just" pressed
    REQUIRE_FALSE(sys.isKeyJustPressed(Key::W));
    REQUIRE(sys.isKeyDown(Key::W));
    REQUIRE_FALSE(sys.isKeyJustReleased(Key::W));
}

TEST_CASE("Key: JustReleased on release, Up after nextFrame", "[InputSystem]") {
    InputSystem sys;

    sys.onKeyEvent({Key::W, true});
    sys.nextFrame();
    sys.onKeyEvent({Key::W, false});

    REQUIRE_FALSE(sys.isKeyDown(Key::W));
    REQUIRE(sys.isKeyJustReleased(Key::W));

    sys.nextFrame();

    REQUIRE_FALSE(sys.isKeyDown(Key::W));
    REQUIRE_FALSE(sys.isKeyJustReleased(Key::W));
}

TEST_CASE("Key: unknown key starts in Up state", "[InputSystem]") {
    InputSystem sys;

    REQUIRE_FALSE(sys.isKeyDown(Key::Space));
    REQUIRE_FALSE(sys.isKeyJustPressed(Key::Space));
    REQUIRE_FALSE(sys.isKeyJustReleased(Key::Space));
}

TEST_CASE("Key: multiple independent keys tracked separately", "[InputSystem]") {
    InputSystem sys;

    sys.onKeyEvent({Key::A, true});
    sys.onKeyEvent({Key::S, true});
    sys.nextFrame();
    sys.onKeyEvent({Key::A, false});

    REQUIRE_FALSE(sys.isKeyDown(Key::A));
    REQUIRE(sys.isKeyJustReleased(Key::A));
    REQUIRE(sys.isKeyDown(Key::S));
    REQUIRE_FALSE(sys.isKeyJustReleased(Key::S));
}

// ── Mouse button state transitions ────────────────────────────────────────────

TEST_CASE("Mouse button: JustPressed / Held / JustReleased lifecycle", "[InputSystem]") {
    InputSystem sys;

    using sonnet::api::input::MouseButtonEvent;
    sys.onMouseEvent(MouseButtonEvent{MouseButton::Left, true});

    REQUIRE(sys.isMouseJustPressed(MouseButton::Left));
    REQUIRE(sys.isMouseDown(MouseButton::Left));

    sys.nextFrame();

    REQUIRE_FALSE(sys.isMouseJustPressed(MouseButton::Left));
    REQUIRE(sys.isMouseDown(MouseButton::Left));

    sys.onMouseEvent(MouseButtonEvent{MouseButton::Left, false});

    REQUIRE(sys.isMouseJustReleased(MouseButton::Left));
    REQUIRE_FALSE(sys.isMouseDown(MouseButton::Left));

    sys.nextFrame();

    REQUIRE_FALSE(sys.isMouseJustReleased(MouseButton::Left));
}

// ── Mouse delta ───────────────────────────────────────────────────────────────

TEST_CASE("Mouse delta is zero before any movement", "[InputSystem]") {
    InputSystem sys;
    const auto d = sys.mouseDelta();
    REQUIRE(d.x == 0.0f);
    REQUIRE(d.y == 0.0f);
}

TEST_CASE("Mouse delta accumulates within a frame and resets after nextFrame", "[InputSystem]") {
    InputSystem sys;

    using sonnet::api::input::MouseMovedEvent;

    // First move establishes position (no delta yet — no previous position)
    sys.onMouseEvent(MouseMovedEvent{{100.0f, 200.0f}});
    sys.nextFrame();

    // Second move produces a delta
    sys.onMouseEvent(MouseMovedEvent{{110.0f, 195.0f}});
    const auto d = sys.mouseDelta();

    REQUIRE(d.x == 10.0f);
    REQUIRE(d.y == -5.0f);

    sys.nextFrame();

    // Delta resets each frame
    const auto d2 = sys.mouseDelta();
    REQUIRE(d2.x == 0.0f);
    REQUIRE(d2.y == 0.0f);
}
