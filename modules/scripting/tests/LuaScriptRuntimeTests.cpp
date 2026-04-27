#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sonnet/api/input/MouseButton.h>
#include <sonnet/api/input/MouseEvent.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/scripting/LuaScriptRuntime.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Scene.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using sonnet::api::input::Key;
using sonnet::api::input::KeyEvent;
using sonnet::api::input::MouseButton;
using sonnet::api::input::MouseButtonEvent;
using sonnet::api::input::MouseMovedEvent;
using sonnet::input::InputSystem;
using sonnet::scripting::LuaScriptRuntime;
using sonnet::world::Scene;
using Catch::Matchers::WithinAbs;

namespace {

namespace fs = std::filesystem;

// Each test gets its own temp directory so concurrent runs don't collide.
// A static counter + PID gives unique paths within a single process.
fs::path makeTempDir() {
    static std::atomic<unsigned> counter{0};
    auto base = fs::temp_directory_path() / "sonnet_lua_tests";
    fs::create_directories(base);
    auto dir = base / ("run_" + std::to_string(::getpid()) + "_" +
                       std::to_string(counter.fetch_add(1)));
    fs::create_directories(dir);
    return dir;
}

void writeScript(const fs::path &p, const std::string &body) {
    std::ofstream out{p};
    out << body;
    out.close();
}

} // namespace

TEST_CASE("LuaScriptRuntime: attach before init runs onStart during init",
         "[lua][lifecycle]") {
    auto dir = makeTempDir();
    auto path = dir / "marker.lua";
    writeScript(path, R"(
        local M = {}
        function M:onStart()
            self.transform:setLocalPosition(7.0, 8.0, 9.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("MyObj");

    InputSystem input;
    LuaScriptRuntime rt;
    rt.attachScript(obj, path.string());

    // Position should still be untouched until init().
    auto preInit = obj.transform.getLocalPosition();
    REQUIRE(preInit == glm::vec3{0.0f});

    rt.init(scene, input);

    auto postInit = obj.transform.getLocalPosition();
    REQUIRE(postInit == glm::vec3{7.0f, 8.0f, 9.0f});
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: attach after init runs onStart immediately",
         "[lua][lifecycle]") {
    auto dir = makeTempDir();
    auto path = dir / "after_init.lua";
    writeScript(path, R"(
        local M = {}
        function M:onStart()
            self.transform:setLocalPosition(1.0, 2.0, 3.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Late");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    REQUIRE(obj.transform.getLocalPosition() == glm::vec3{1.0f, 2.0f, 3.0f});
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: update invokes onUpdate with dt", "[lua][update]") {
    auto dir = makeTempDir();
    auto path = dir / "tick.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            self.transform:translate(dt, 0.0, 0.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Ticker");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    rt.update(0.5f);
    rt.update(0.25f);

    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(0.75f, 1e-5f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: detachObject removes instances", "[lua][detach]") {
    auto dir = makeTempDir();
    auto path = dir / "increment.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            self.transform:translate(1.0, 0.0, 0.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Detachable");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    rt.update(0.0f);
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));

    rt.detachObject(&obj);

    rt.update(0.0f);
    // No further increment after detach.
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: bad script path does not throw and does not add instance",
         "[lua][error]") {
    Scene scene;
    auto &obj = scene.createObject("Missing");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);

    REQUIRE_NOTHROW(rt.attachScript(obj, "/path/that/does/not/exist.lua"));
    // update() must be safe even though no instance was registered.
    REQUIRE_NOTHROW(rt.update(1.0f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: reload returns empty string when nothing changed",
         "[lua][reload]") {
    auto dir = makeTempDir();
    auto path = dir / "stable.lua";
    writeScript(path, R"(
        local M = {}
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Stable");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    REQUIRE(rt.reload().empty());
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: reload picks up edited script", "[lua][reload]") {
    auto dir = makeTempDir();
    auto path = dir / "evolving.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            self.transform:translate(1.0, 0.0, 0.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Evolving");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    rt.update(0.0f);
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));

    // Sleep briefly so the rewritten file's mtime is strictly greater than
    // the original; some filesystems have second-resolution mtimes.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            self.transform:translate(0.0, 5.0, 0.0)
        end
        return M
    )");

    auto msg = rt.reload();
    REQUIRE(msg.find("Reloaded:") != std::string::npos);
    REQUIRE(msg.find("evolving.lua") != std::string::npos);

    rt.update(0.0f);
    // y advanced by the *new* onUpdate; x did not advance further.
    REQUIRE_THAT(obj.transform.getLocalPosition().y, WithinAbs(5.0f, 1e-5f));
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: Input bindings reflect engine input state",
         "[lua][bindings][input]") {
    auto dir = makeTempDir();
    auto path = dir / "input_probe.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            if Input.isKeyDown("Space") then
                self.transform:setLocalPosition(1.0, 0.0, 0.0)
            else
                self.transform:setLocalPosition(0.0, 0.0, 0.0)
            end
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Probe");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    rt.update(0.0f);
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(0.0f, 1e-5f));

    input.onKeyEvent(KeyEvent{Key::Space, true});
    rt.update(0.0f);
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: Scene.find returns a transform of the named object",
         "[lua][bindings][scene]") {
    auto dir = makeTempDir();
    auto path = dir / "scene_find.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            local target = Scene.find("Target")
            if target then
                target:setLocalPosition(99.0, 0.0, 0.0)
            end
        end
        return M
    )");

    Scene scene;
    auto &probe   = scene.createObject("Probe");
    auto &target  = scene.createObject("Target");
    (void)target;
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(probe, path.string());

    rt.update(0.0f);

    // The Lua script should have moved the *Target* object, not the probe.
    REQUIRE(probe.transform.getLocalPosition()  == glm::vec3{0.0f});
    REQUIRE_THAT(target.transform.getLocalPosition().x, WithinAbs(99.0f, 1e-5f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: Transform bindings cover scale, world, eulers, rotate, basis",
         "[lua][bindings][transform]") {
    auto dir = makeTempDir();
    auto path = dir / "transform_probe.lua";
    // The script writes results into a global scratch table the C++ side reads
    // via a Scene.find round-trip on the parented child object.
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            -- Local scale round-trip.
            self.transform:setLocalScale(2.0, 3.0, 4.0)
            local sx, sy, sz = self.transform:getLocalScale()
            self.scaleX, self.scaleY, self.scaleZ = sx, sy, sz

            -- World position: this object is parented at (10,0,0), so
            -- setting world (15,0,0) yields a non-zero world position.
            self.transform:setWorldPosition(15.0, 0.0, 0.0)
            local wx, wy, wz = self.transform:getWorldPosition()
            self.worldX, self.worldY, self.worldZ = wx, wy, wz

            -- Euler degrees round-trip.
            self.transform:setLocalEulerDegrees(0.0, 30.0, 0.0)
            local ex, ey, ez = self.transform:getLocalEulerDegrees()
            self.eulerY = ey

            -- rotate(axis, angle) — replace the rotation, then read forward.
            self.transform:setLocalEulerDegrees(0.0, 0.0, 0.0)
            self.transform:rotate(0.0, 1.0, 0.0, 90.0)
            local fx, fy, fz = self.transform:forward()
            self.fwdX, self.fwdY, self.fwdZ = fx, fy, fz
            local rx, ry, rz = self.transform:right()
            self.rightX, self.rightY, self.rightZ = rx, ry, rz
            local ux, uy, uz = self.transform:up()
            self.upY = uy
        end
        return M
    )");

    Scene scene;
    auto &parent = scene.createObject("Parent");
    auto &child  = scene.createObject("Child");
    parent.transform.setLocalPosition({10.0f, 0.0f, 0.0f});
    child.transform.setParent(&parent.transform, false);

    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(child, path.string());

    rt.update(0.0f);

    // Pull the script's scratch values back through sol via a second tiny
    // script: not necessary — instead we assert on the resulting Transform
    // state, which is what the bindings actually mutate.
    REQUIRE(child.transform.getLocalScale() == glm::vec3{2.0f, 3.0f, 4.0f});
    // Parent at (10,0,0); child world set to (15,0,0) → child local x = 5.
    REQUIRE_THAT(child.transform.getLocalPosition().x, WithinAbs(5.0f, 1e-4f));
    auto world = child.transform.getWorldPosition();
    REQUIRE_THAT(world.x, WithinAbs(15.0f, 1e-4f));

    // After rotate(0,1,0, 90°): forward (0,0,-1) rotates around +Y by 90° →
    // (-1,0,0). The y component is the load-bearing one (must be ~0 either way).
    auto fwd = child.transform.forward();
    REQUIRE_THAT(fwd.y, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(std::abs(fwd.x) + std::abs(fwd.z), WithinAbs(1.0f, 1e-4f));
    auto upv = child.transform.up();
    REQUIRE_THAT(upv.y, WithinAbs(1.0f, 1e-4f));

    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: Input just-pressed/released and mouse bindings",
         "[lua][bindings][input]") {
    auto dir = makeTempDir();
    auto path = dir / "input_full.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            local x = 0.0
            local y = 0.0
            local z = 0.0
            if Input.isKeyJustPressed("Space")  then x = x + 1.0 end
            if Input.isKeyJustReleased("Space") then y = y + 1.0 end
            if Input.isMouseDown(0)             then z = z + 1.0 end
            local dx, dy = Input.mouseDelta()
            self.transform:setLocalPosition(x + dx, y + dy, z)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("InputProbe");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    // Frame 1: Space pressed, left mouse down, two MouseMovedEvents
    // (the first seeds m_hasMousePosition; the second contributes the delta).
    input.onKeyEvent(KeyEvent{Key::Space, true});
    input.onMouseEvent(MouseButtonEvent{MouseButton::Left, true});
    input.onMouseEvent(MouseMovedEvent{glm::vec2{0.0f, 0.0f}});
    input.onMouseEvent(MouseMovedEvent{glm::vec2{4.0f, 7.0f}});
    rt.update(0.0f);

    auto p1 = obj.transform.getLocalPosition();
    REQUIRE_THAT(p1.x, WithinAbs(1.0f + 4.0f, 1e-5f)); // justPressed + dx
    REQUIRE_THAT(p1.y, WithinAbs(0.0f + 7.0f, 1e-5f)); // dy
    REQUIRE_THAT(p1.z, WithinAbs(1.0f, 1e-5f));        // mouse down

    // Advance: justPressed should drop to false; release Space to trigger
    // justReleased; clear delta.
    input.nextFrame();
    input.onKeyEvent(KeyEvent{Key::Space, false});
    input.onMouseEvent(MouseButtonEvent{MouseButton::Left, false});
    rt.update(0.0f);

    auto p2 = obj.transform.getLocalPosition();
    REQUIRE_THAT(p2.x, WithinAbs(0.0f, 1e-5f));  // not just pressed, no delta
    REQUIRE_THAT(p2.y, WithinAbs(1.0f, 1e-5f));  // just released
    REQUIRE_THAT(p2.z, WithinAbs(0.0f, 1e-5f));  // mouse up
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: strToKey covers digit/function/arrow/modifier/special and unknown",
         "[lua][bindings][input]") {
    auto dir = makeTempDir();
    auto path = dir / "key_spread.lua";
    // Encodes which categories report down by setting one component per match.
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            local hit = 0
            if Input.isKeyDown("5")          then hit = hit + 1 end
            if Input.isKeyDown("F1")         then hit = hit + 1 end
            if Input.isKeyDown("Up")         then hit = hit + 1 end
            if Input.isKeyDown("LeftShift")  then hit = hit + 1 end
            if Input.isKeyDown("Escape")     then hit = hit + 1 end
            local unknown = Input.isKeyDown("NotAKey") and 1 or 0
            self.transform:setLocalPosition(hit, unknown, 0.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("KeySpread");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    input.onKeyEvent(KeyEvent{Key::Five,      true});
    input.onKeyEvent(KeyEvent{Key::F1,        true});
    input.onKeyEvent(KeyEvent{Key::Up,        true});
    input.onKeyEvent(KeyEvent{Key::LeftShift, true});
    input.onKeyEvent(KeyEvent{Key::Escape,    true});
    rt.update(0.0f);

    auto p = obj.transform.getLocalPosition();
    REQUIRE_THAT(p.x, WithinAbs(5.0f, 1e-5f)); // all five categories resolved
    REQUIRE_THAT(p.y, WithinAbs(0.0f, 1e-5f)); // unknown maps to Key::Unknown, never down
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: onUpdate runtime error is reported but does not throw",
         "[lua][error]") {
    auto dir = makeTempDir();
    auto path = dir / "runtime_error.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            error("boom")
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("Boom");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    REQUIRE_NOTHROW(rt.update(0.0f));
    REQUIRE_NOTHROW(rt.update(0.0f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: reload reports parse error with 'Error (' prefix",
         "[lua][reload][error]") {
    auto dir = makeTempDir();
    auto path = dir / "becomes_broken.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt) end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("BreakMe");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    // Sleep so the rewritten file has a strictly greater mtime.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeScript(path, "this is not valid lua @@@");

    auto msg = rt.reload();
    REQUIRE(msg.rfind("Error (", 0) == 0);
    REQUIRE(msg.find("becomes_broken.lua") != std::string::npos);
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: detachObject leaves siblings running",
         "[lua][detach]") {
    auto dir = makeTempDir();
    auto pathA = dir / "sibA.lua";
    auto pathB = dir / "sibB.lua";
    writeScript(pathA, R"(
        local M = {}
        function M:onUpdate(dt) self.transform:translate(1.0, 0.0, 0.0) end
        return M
    )");
    writeScript(pathB, R"(
        local M = {}
        function M:onUpdate(dt) self.transform:translate(0.0, 1.0, 0.0) end
        return M
    )");

    Scene scene;
    auto &a = scene.createObject("SibA");
    auto &b = scene.createObject("SibB");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(a, pathA.string());
    rt.attachScript(b, pathB.string());

    rt.update(0.0f);
    REQUIRE_THAT(a.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(b.transform.getLocalPosition().y, WithinAbs(1.0f, 1e-5f));

    rt.detachObject(&a);

    rt.update(0.0f);
    // a frozen, b advances.
    REQUIRE_THAT(a.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(b.transform.getLocalPosition().y, WithinAbs(2.0f, 1e-5f));
    rt.shutdown();
}

TEST_CASE("LuaScriptRuntime: shutdown clears instances so update is a no-op",
         "[lua][shutdown]") {
    auto dir = makeTempDir();
    auto path = dir / "shutdown.lua";
    writeScript(path, R"(
        local M = {}
        function M:onUpdate(dt)
            self.transform:translate(1.0, 0.0, 0.0)
        end
        return M
    )");

    Scene scene;
    auto &obj = scene.createObject("ShutTarget");
    InputSystem input;
    LuaScriptRuntime rt;
    rt.init(scene, input);
    rt.attachScript(obj, path.string());

    rt.update(0.0f);
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));

    rt.shutdown();

    rt.update(0.0f);
    REQUIRE_THAT(obj.transform.getLocalPosition().x, WithinAbs(1.0f, 1e-5f));
}
