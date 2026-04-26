# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Sonnet is a modern cross-platform 3D game engine built with C++23 and CMake. It uses a modular architecture with strict one-way dependencies between modules. Dependencies are managed via CMake `FetchContent` for automatic setup.

The reference documentation lives under [`docs/`](docs/) — start at [`docs/architecture.md`](docs/architecture.md) for the module map, and [`README.md`](README.md) for the full doc index. Read the relevant feature doc before making non-trivial changes.

## Build system

CMake (≥ 3.25) is the build system. Dependencies are fetched via `FetchContent` — no manual installation needed beyond the Vulkan SDK when building the Vulkan backend.

- C++23 with warnings-as-errors. Third-party targets (assimp, ImGui, Jolt) have warnings-as-errors locally disabled — do not extend that pattern to engine code.
- In-source builds are blocked.
- The renderer backend is chosen at configure time via `SONNET_RENDERER_BACKEND = Auto | OpenGL | Vulkan` (`Auto` → Vulkan on Apple, OpenGL elsewhere). See [docs/build.md](docs/build.md).
- Compile definitions `SONNET_USE_OPENGL` / `SONNET_USE_VULKAN` are emitted from CMake; reference them via `#if defined(SONNET_USE_VULKAN)`.

Common commands:

```bash
cmake -S . -B build -DSONNET_RENDERER_BACKEND=OpenGL
cmake --build build
ctest --test-dir build
```

The user has pre-configured `cmake-build-debug-opengl/` and `cmake-build-debug-vulkan/` directories. Build commands (`cmake --build`, `ctest`) may be run without asking for permission.

## Architecture

The engine is organised as small modules with strict one-way dependencies. The order in `modules/CMakeLists.txt` is the canonical dependency direction:

```
core → api → input → window → loaders → world → physics → scripting
                                              ↘ renderer (frontend ↔ backend) ↘
                                                                 primitives → scene → ui
```

A module may depend on anything to its left, never on anything to its right. `api` is header-only interfaces (`IRenderer`, `IRendererBackend`, `IWindow`, `IInput`, …); concrete implementations live in their own modules.

### Renderer

`renderer/frontend/Renderer` is the high-level API. It owns one `IRendererBackend` (`opengl/GlRendererBackend` or `vulkan/VkRendererBackend`) and manages typed-handle stores for meshes, shaders, materials, textures, and render targets. The single `#if`-switched site is `renderer/frontend/src/BackendFactory.cpp` — keep it that way.

When adding a new backend operation:

1. Add a virtual to `IRendererBackend` (or its companion factory interfaces in the same header).
2. Implement it in **both** `GlRendererBackend` and `VkRendererBackend`. The Vulkan backend's lazy pipeline / descriptor / uniform-ring caches are nontrivial; consult [`vulkan-impl.md`](vulkan-impl.md) for the Vulkan path before changes.
3. Build matrix-affecting changes through `core/RendererTraits.h` (`presets::OpenGL`, `presets::Vulkan`) so NDC Z range and clip-space Y flips track the active backend automatically.

### Resource handles

`Handle<Tag>` in `core/Handle.h` wraps a `size_t`; the tag prevents cross-type assignments. The frontend `Renderer` issues all engine handles. Treat `borrow`-style handles produced by `colorTextureHandle` / `depthTextureHandle` as cheap to keep on materials — they survive `resizeRenderTarget`.

### Scene graph

`world::Scene` owns `GameObject`s flatly; `Transform`s form the hierarchy via parent pointers. Components on `GameObject` are `std::optional<...>` (Render, Light, Camera, AnimationPlayer, Skin). `Scene::buildRenderQueue` does frustum culling against component bounding spheres. See [docs/world.md](docs/world.md).

## Demo (`apps/demo/`)

The demo is the reference integration of every engine feature: deferred shading, CSM + point shadows, IBL, SSAO, SSR, bloom, FXAA, outline, mouse picking, ImGui editor with gizmos, Lua scripting, physics, animation/skinning, JSON scene loading + hot-reload. See [docs/demo.md](docs/demo.md) and the feature-specific docs linked from [README.md](README.md).

`apps/demo/RenderGraph.{h,cpp}` and `PostProcess.{h,cpp}` orchestrate the post-process passes. The render graph is **demo-specific** — do not move it into the engine without an explicit request.

The demo currently still has small raw-OpenGL fragments (point-light shadow cubemaps in `ShadowMaps.cpp`, IBL bake in `IBL.h`). Those paths gate on `SONNET_USE_OPENGL` and the Vulkan equivalents are tracked in `vulkan-impl.md`. Avoid adding new raw-GL code unless extending one of these existing paths.

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2), fetched via CMake `FetchContent`. Each module with tests builds a `<module>_tests` executable through the `sonnet_add_module_test` helper in `cmake/functions.cmake`. New tests should follow the same helper. Tests can include the module's `src/` directory for white-box testing.

## Code style

- Follow the existing comment density: comments explain *why* something exists when it is non-obvious (constraints, NDC handedness, std140 padding, lifecycle ordering, third-party quirks). Don't add comments that restate the code.
- Don't introduce backwards-compatibility shims, feature flags, or "future-proof" abstractions ahead of need. The codebase favours a clean break when something changes.
- New shaders go under `apps/demo/assets/shaders/` only if they're demo-specific. Engine code should not bundle assets.
- When touching the renderer, run the demo against **both** backends (`-DSONNET_RENDERER_BACKEND=OpenGL` and `Vulkan`) before claiming success — backend-specific bugs are the highest-cost regressions.

## Known constraints

- Skinned meshes upload bone palettes into `uBoneMatrices[i]` per-frame from the application; this is not done automatically by `Renderer::render`. See `apps/demo/main.cpp` for the loop.
- The demo's point-light shadow path uses raw GL geometry-shader fan-out; it is OpenGL-only until the Vulkan cubemap-layered render path lands.
- IBL bake (`apps/demo/IBL.h`) is OpenGL-only and runs through glad directly.
