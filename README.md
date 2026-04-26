# Sonnet

Sonnet is a modern, cross-platform 3D game engine written in C++23 with CMake. It is built around a small set of decoupled modules with strict one-way dependencies and a swappable low-level renderer (OpenGL or Vulkan, selected at configure time).

## Highlights

- **Two renderer backends behind one interface** — OpenGL 4.6 (glad) and Vulkan 1.3 (VMA + glslang + SPIRV-Reflect). Backend selection is compile-time and the only `#if`-switched site is `BackendFactory.cpp`.
- **Typed-handle resource model** — meshes, shaders, materials, textures, and render targets are referenced by tag-stamped opaque handles produced by the frontend `Renderer`.
- **Material templates + instances** — shared shader + render state + default uniforms, with cheap per-object override maps.
- **Scene graph** — `Scene` of `GameObject`s, parent/child `Transform`s, `Render`/`Light`/`Camera`/`AnimationPlayer`/`Skin` components.
- **Asset pipeline** — Assimp model loader (glTF/GLB + OBJ) with PBR materials, skinning, animation clips; stb-based texture loader; GLSL shader loader with hot-reload in the demo.
- **JSON scenes** — declarative `scene.json` format describing assets, objects, lights, physics, scripts (see [scene.schema.json](scene.schema.json)).
- **Lua scripting** — sol2-bound `IScriptRuntime` with `Transform`, `Input`, `Scene`, `Log` APIs and live file reload.
- **Physics** — Jolt-backed `PhysicsSystem` with static / dynamic / kinematic bodies, box and sphere shapes.
- **ImGui editor** (in the demo) — viewport, hierarchy, inspector, asset list, render settings, gizmos, mouse picking, scene save.
- **Built-in primitives** — programmatic box, UV sphere, quad mesh builders.
- **FetchContent for everything** — except the Vulkan SDK, which the host must provide when building the Vulkan backend.

## Project layout

```
modules/
├── core         — handles, hashing, type aliases, renderer traits, generic Store<T>
├── api          — header-only interfaces (IRenderer, IRendererBackend, IWindow, IInput, ...)
├── input        — InputSystem (concrete IInput / IInputSink)
├── window       — GLFW window + input adapter
├── loaders      — TextureLoader, ShaderLoader, ModelLoader (Assimp)
├── world        — Scene, GameObject, Transform, Camera/Render/Light/Skin/Animation components
├── physics      — Jolt-backed PhysicsSystem
├── scripting    — IScriptRuntime + Lua (sol2) implementation
├── renderer     ── frontend (Renderer + UBO layouts)
│                ├─ opengl  (glad-based backend)
│                └─ vulkan  (Vulkan 1.3 + VMA + glslang + SPIRV-Reflect)
├── primitives   — programmatic CPUMesh builders
├── scene        — JSON SceneLoader
└── ui           — ImGuiLayer (GLFW + GL3 / Vulkan)

apps/
└── demo/        — reference application (see docs/demo.md)

cmake/           — options.cmake, dependencies.cmake, functions.cmake
docs/            — feature-by-feature documentation
scene.schema.json — JSON Schema for scene files
vulkan-impl.md   — detailed Vulkan backend implementation status
```

## Build

```bash
# OpenGL build
cmake -S . -B build -DSONNET_RENDERER_BACKEND=OpenGL
cmake --build build

# Vulkan build (requires the Vulkan SDK on the host)
cmake -S . -B build-vk -DSONNET_RENDERER_BACKEND=Vulkan
cmake --build build-vk

# Run the demo
./build/apps/demo/sonnet_demo

# Run all unit tests
ctest --test-dir build
```

`SONNET_RENDERER_BACKEND` accepts `Auto | OpenGL | Vulkan` (default `Auto`: Vulkan on Apple, OpenGL elsewhere). See [docs/build.md](docs/build.md) for the full option set.

## Documentation

| Topic | Document |
|---|---|
| **Engine architecture** | [docs/architecture.md](docs/architecture.md) |
| Build system & dependencies | [docs/build.md](docs/build.md) |
| Renderer (frontend + backend interface) | [docs/renderer.md](docs/renderer.md) |
| OpenGL backend | [docs/opengl-backend.md](docs/opengl-backend.md) |
| Vulkan backend (overview) | [docs/vulkan-backend.md](docs/vulkan-backend.md) |
| Vulkan backend (detailed status) | [vulkan-impl.md](vulkan-impl.md) |
| Window & input | [docs/window-input.md](docs/window-input.md) |
| World / scene graph | [docs/world.md](docs/world.md) |
| Asset loaders | [docs/loaders.md](docs/loaders.md) |
| Scene file format | [docs/scene-files.md](docs/scene-files.md) |
| Mesh primitives | [docs/primitives.md](docs/primitives.md) |
| Physics (Jolt) | [docs/physics.md](docs/physics.md) |
| Lua scripting | [docs/scripting.md](docs/scripting.md) |
| ImGui UI layer | [docs/ui.md](docs/ui.md) |
| **Demo application** | [docs/demo.md](docs/demo.md) |
| Render graph (demo) | [docs/render-graph.md](docs/render-graph.md) |
| Post-processing pipeline (demo) | [docs/post-process.md](docs/post-process.md) |
| Shadow maps — CSM + point cubemaps (demo) | [docs/shadow-maps.md](docs/shadow-maps.md) |
| Image-based lighting (demo) | [docs/ibl.md](docs/ibl.md) |

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2). Each module that has tests builds a `<module>_tests` executable via the `sonnet_add_module_test` helper in `cmake/functions.cmake`. They are registered with CTest, so `ctest --test-dir build` runs the full suite.

## License

(no license file in the repository at this time)
