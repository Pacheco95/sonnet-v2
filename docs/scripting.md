# Scripting

`modules/scripting/` provides `IScriptRuntime`, a language-agnostic scripting interface, and a Lua 5.4 implementation built on sol2.

## IScriptRuntime

Defined in `scripting/IScriptRuntime.h`. The runtime is owned by the application and threaded through the frame loop:

```cpp
LuaScriptRuntime scripts;
scripts.attachScript(obj, "scripts/rotate.lua");   // before or after init()
scripts.init(scene, input);                          // calls onStart on attached scripts
// per frame:
scripts.update(dt);
const auto reloadMsg = scripts.reload();             // hot-reload changed files
// on object destruction:
scripts.detachObject(&obj);
// on shutdown:
scripts.shutdown();
```

`reload()` returns a human-readable string (`""`, `"Reloaded: foo.lua"`, or `"Error (foo.lua): ..."`). The demo displays it in the menu bar as a transient notification.

A future C# runtime (or any other language) implements the same interface; main.cpp wires it through the same calls.

## Lua bindings

`LuaScriptRuntime` opens `base`, `math`, `string`, and `table` Lua libraries, then exposes:

### `Transform` usertype

```lua
t:getLocalPosition()      -- returns x, y, z
t:setLocalPosition(x, y, z)
t:getWorldPosition()      -- returns x, y, z
t:setWorldPosition(x, y, z)
t:getLocalScale() / setLocalScale(x, y, z)
t:getLocalEulerDegrees() / setLocalEulerDegrees(x, y, z)
t:rotate(ax, ay, az, deg)
t:translate(x, y, z)
t:forward() / right() / up()    -- each returns x, y, z
```

### `Input` table

```lua
Input.isKeyDown("W"), Input.isKeyJustPressed("Space"), Input.isKeyJustReleased("Escape")
Input.isMouseDown(0)            -- 0 = Left, 1 = Right, 2 = Middle
Input.mouseDelta()              -- returns dx, dy
```

Key names mirror the engine's `Key` enum: letters `"A"–"Z"`, digits `"0"–"9"`, `"Space"`, `"Enter"`, `"Escape"`, `"Tab"`, `"Backspace"`, arrows (`"Up"`, `"Down"`, `"Left"`, `"Right"`), modifiers (`"LeftShift"`, `"LeftControl"`, etc.), function keys `"F1"–"F12"`.

### `Scene` table

```lua
Scene.find("ObjectName")        -- returns Transform or nil
```

### `Log` table

```lua
Log.info("…"), Log.warn("…"), Log.error("…")
```

## Script structure

A script file returns a table with `onStart` / `onUpdate` methods. The runtime treats the table itself as the per-instance "self", so members written in `onStart` persist across frames:

```lua
-- scripts/rotate.lua
local Rotate = {}
Rotate.__index = Rotate

function Rotate:onStart()
    self.speed = 45.0
    Log.info("Rotate started on " .. self.name)
end

function Rotate:onUpdate(dt)
    self.transform:rotate(0, 1, 0, self.speed * dt)
end

return Rotate
```

The runtime injects `self.name` (the GameObject's name) and `self.transform` (its `Transform`) automatically before calling `onStart`. Scripts can also call any of the global tables above (`Input`, `Scene`, `Log`).

## Hot-reload

`LuaScriptRuntime` records the filesystem mtime of every attached script. `reload()` re-reads any file whose mtime advanced, re-runs it to obtain a new metatable, and replaces the script's behaviour on every attached instance — `onStart` is *not* re-invoked. Compile-time errors in the new file leave the previous version active and produce an error string.
