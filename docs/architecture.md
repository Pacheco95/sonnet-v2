# Architecture

Sonnet is organised as a set of small modules with strict one-way dependencies. Every module lives under `modules/<name>/` and exports its public headers under `sonnet/<name>/...`.

## Module layout

```
core         — handles, hashing, type aliases, renderer traits, generic Store<T>
api          — header-only interfaces (IRenderer, IRendererBackend, IWindow, IInput, ...)
input        — concrete InputSystem (implements api::input::IInput / IInputSink)
window       — GLFW-backed IWindow + GLFW input adapter
loaders      — TextureLoader (stb_image), ShaderLoader, ModelLoader (Assimp)
world        — Scene, GameObject, Transform, CameraComponent, AnimationPlayer, SkinComponent
physics      — Jolt-backed PhysicsSystem
scripting    — IScriptRuntime + Lua implementation (sol2)
renderer     — frontend (Renderer) + opengl/ + vulkan/ backends
primitives   — programmatic CPUMesh builders (box, UV sphere, quad)
scene        — JSON SceneLoader (assets, objects, materials, lights, physics, scripts)
ui           — ImGuiLayer (GLFW + GL3 / Vulkan backend)
```

## Dependency direction

`modules/CMakeLists.txt` adds the modules in dependency order:

```
core → api → input → window → loaders → world → physics → scripting
                                              ↘ renderer (frontend ↔ backend) ↘
                                                                 primitives → scene → ui
```

The contract: a module may depend on anything to its left, never on anything to its right. Concretely:

- `api` only depends on `core` and external math (glm). It defines pure interfaces; no implementation lives here.
- `input` and `window` are independent of the renderer.
- `world` knows about `api::render` types (so a `RenderComponent` can hold a material instance) but not the backend.
- `renderer/frontend` is the only module that talks to `IRendererBackend`. Backends never depend on the frontend.
- `scene` depends on `world`, `loaders`, `physics`, and `renderer/frontend`; it ties them together.

## Backend selection

The renderer backend is chosen at CMake configuration time via `SONNET_RENDERER_BACKEND` (`Auto | OpenGL | Vulkan`). `Auto` picks Vulkan on Apple targets and OpenGL elsewhere. The choice is exposed as compile definitions `SONNET_USE_OPENGL` / `SONNET_USE_VULKAN`, and `cmake/dependencies.cmake` adds backend-specific dependencies (Vulkan + VMA + SPIRV-Reflect, or glad) only when needed.

The single `#if`-switched site in the engine is `renderer/frontend/src/BackendFactory.cpp`, which `makeBackend()` returns either `GlRendererBackend` or `VkRendererBackend`. Everything above the backend interface is build-flavour agnostic.

## Resource handles

Engine resources are referenced by typed opaque handles defined in `core/Types.h`:

```
ShaderHandle, MaterialTemplateHandle, GPUMeshHandle, GPUTextureHandle,
RenderTargetHandle, CPUMeshHandle, CPUTextureBufferHandle
```

Each `Handle<Tag>` wraps a `std::size_t`; the tag prevents accidental cross-type assignments at compile time. Handles are produced by the frontend `Renderer` (`createMesh`, `createShader`, `createMaterial`, `createTexture`, `createRenderTarget`). The backing GPU object lives inside the renderer's per-handle `unordered_map`; the handle itself is cheap to copy and store on `RenderItem`s and `MaterialInstance`s.

## See also

- [Build system](build.md)
- [Renderer](renderer.md)
- [Vulkan backend](vulkan-backend.md) / [OpenGL backend](opengl-backend.md)
- [World & scene graph](world.md)
