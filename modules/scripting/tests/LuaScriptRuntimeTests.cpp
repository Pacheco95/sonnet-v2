#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
