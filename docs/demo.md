# Demo application

`apps/demo/` is the engine's reference application. It exercises every implemented feature: the renderer (both backends), shadow maps, IBL, deferred lighting, post-processing, physics, scripting, animation/skinning, an ImGui-based editor with selection/gizmos/picking, and JSON scene loading with hot-reload.

## Layout

```
apps/demo/
├── main.cpp           — frame-loop driver, ties subsystems together
├── main_vk.cpp        — Vulkan-specific entry (used during backend bring-up)
├── ShaderRegistry.{h,cpp}  — compile + 0.5 s-poll hot-reload for GLSL files
├── ShadowMaps.{h,cpp}      — CSM + point-light cubemap shadows
├── RenderTargets.{h,cpp}   — all screen-sized RTs + handle bookkeeping
├── RenderGraph.{h,cpp}     — declarative pass scheduler (see render-graph.md)
├── PostProcess.{h,cpp}     — pass material instances + per-frame execute
├── IBL.h                   — environment map → irradiance/prefilter/BRDF LUT
├── EditorUI.{h,cpp}        — ImGui editor: panels, gizmos, picking
├── FlyCamera.h             — WASD-EQ + mouse-look transform driver
└── assets/
    ├── scene.json          — scene file consumed by SceneLoader
    ├── shaders/            — one .vert/.frag per pass (+ ibl/ subdir)
    ├── scripts/rotate.lua  — example Lua script
    └── models/             — glTF/GLB samples (DamagedHelmet, Fox, BusterDrone, AnimatedCube)
```

## Frame loop

The high-level structure of `main.cpp`:

1. Create `GLFWWindow`, `InputSystem`, backend, ImGui layer, `Renderer`.
2. Build `ShaderRegistry`, `ShadowMaps`, IBL, `PhysicsSystem`, `Scene`, `SceneLoader`, `LuaScriptRuntime`.
3. Build screen-sized `RenderTargets` (also generates the SSAO kernel + noise).
4. Build `PostProcess` (one material instance per pass, plus a render graph).
5. Build `FlyCamera` and `EditorUI`.

Every frame:

1. `window.pollEvents()`, `registry.tick(dt)` (shader hot-reload), `scriptRuntime.reload()` (Lua hot-reload).
2. Resize all RTs if framebuffer size changed (handles remain valid).
3. Update camera (RMB + viewport focused → captures cursor and runs `FlyCamera::update`).
4. Animate the scene: scripted `Arm` rotation, Lua scripts, physics step, animation players, skinning palette upload.
5. Build the `FrameContext` (camera matrices, lights pulled from `LightComponent`s).
6. `shadows.render(...)` → CSM cascades + point-light cubemaps.
7. `pp.execute(...)` → G-buffer → SSAO → deferred → sky → SSR → bloom → tonemap → FXAA → outline → viewport.
8. Bind the default RT, clear it, draw the editor (which `ImGui::Image`s the viewport texture).
9. `imgui.end()`, `window.swapBuffers()`, `input.nextFrame()`.

## Editor controls

| Control | Action |
|---|---|
| Right-mouse drag in viewport | Hold to capture cursor → mouse-look |
| `W A S D` | Forward / strafe (only while RMB held) |
| `E` / `Q` | Up / down (world Y) |
| Left-click on viewport | Picking (selects object under cursor) |
| Translate / Rotate / Scale gizmo | Click + drag axis handles in viewport |
| Hierarchy panel | Tree view; click to select; right-click for duplicate / destroy |
| Inspector panel | Edit transform/render/light/camera/physics components |
| Render Settings panel | Live tunables for every post-process pass |
| File → Save scene | Round-trip the scene back to its JSON file |
| `Esc` | Quit |

## Scene file

`apps/demo/assets/scene.json` populates a small environment with:

- A directional light + a couple of point lights (one driven by the `Lamp` emissive material).
- Static floor + walls, a rotating `Arm` parent, several PBR-textured models (`DamagedHelmet`, `Fox`, `BusterDrone`).
- Skinned and animated `Fox` and `BusterDrone` (`AnimationPlayer` + `SkinComponent`).
- A `Camera` object with a `CameraComponent`.
- Lua-scripted rotation via `scripts/rotate.lua`.

See [Scene file format](scene-files.md) for the schema.

## Hot-reload

`ShaderRegistry::tick(dt)` polls every registered shader's `.vert` and `.frag` mtime every ~0.5 s. When a file changes it calls `Renderer::reloadShader(handle, vertSrc, fragSrc)`; on success it returns `"Reloaded: foo.vert"` (displayed in the menu bar), on failure it returns `"Error (foo.frag): <log>"` and keeps the previous shader live.

`LuaScriptRuntime::reload()` does the equivalent for `.lua` files: changed scripts are re-parsed and the new metatable is swapped onto every attached instance, without re-running `onStart`.
