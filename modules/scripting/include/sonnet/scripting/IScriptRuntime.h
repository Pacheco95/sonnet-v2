#pragma once

#include <string>

// Forward declarations — keeps this header free of heavy engine includes so
// a future C# runtime can include it without pulling in Lua or sol2.
namespace sonnet::world { class Scene; class GameObject; }
namespace sonnet::input { class InputSystem; }

namespace sonnet::scripting {

// Language-agnostic scripting runtime interface.
//
// Lua implementation: LuaScriptRuntime (sol2).
// Future C# implementation: instantiate .NET 8 hosted runtime, map
//   attachScript → class-by-filename convention, update → OnUpdate delegate.
// Callers in main.cpp never need to change when the runtime is swapped.
class IScriptRuntime {
public:
    virtual ~IScriptRuntime() = default;

    // Called once after the scene is loaded.
    // Provides the runtime with access to engine services and calls onStart
    // on every script that was already attached via attachScript().
    virtual void init(sonnet::world::Scene          &scene,
                      const sonnet::input::InputSystem &input) = 0;

    // Attach a script file to a specific game object.
    // May be called before or after init(). If called before init(), onStart
    // will be invoked during init(). If called after, onStart is invoked
    // immediately.
    virtual void attachScript(sonnet::world::GameObject &obj,
                               const std::string         &scriptPath) = 0;

    // Tick all active script instances. Call once per frame.
    virtual void update(float dt) = 0;

    // Remove all script instances attached to obj. Call before destroying the
    // game object to prevent dangling pointers inside the runtime.
    // No-op default provided for runtimes that don't need it.
    virtual void detachObject(sonnet::world::GameObject *obj) {}

    // Check attached script files for changes and hot-reload any that have
    // been modified. Returns a human-readable notification string:
    //   ""                  — nothing changed
    //   "Reloaded: foo.lua" — at least one script was reloaded successfully
    //   "Error (foo.lua): …"— the last reload attempt failed
    // A no-op default is provided so a future C# runtime can opt out.
    virtual std::string reload() { return {}; }

    // Release all Lua/runtime state.
    virtual void shutdown() = 0;
};

} // namespace sonnet::scripting
