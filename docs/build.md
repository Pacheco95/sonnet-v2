# Build system

Sonnet uses CMake (≥ 3.25) with `FetchContent` for almost all dependencies. The only host-supplied dependency is the Vulkan SDK (only when building the Vulkan backend).

## Standards & flags

- C++23 (`CMAKE_CXX_STANDARD 23`, no compiler extensions).
- `CMAKE_COMPILE_WARNING_AS_ERROR ON`. Third-party targets that don't compile cleanly under our flags (assimp, ImGui, Jolt) have warning-as-error disabled per-target.
- In-source builds are blocked.

## Configure-time options

Defined in `cmake/options.cmake`:

| Option | Default | Notes |
|---|---|---|
| `SONNET_BUILD_TESTS` | `ON` | Builds Catch2 test executables for each module that has them. |
| `SONNET_RENDERER_BACKEND` | `Auto` | `Auto`, `OpenGL`, or `Vulkan`. `Auto` → Vulkan on Apple, OpenGL elsewhere. |

The resolved backend is exported as the compile definitions `SONNET_USE_OPENGL=1` or `SONNET_USE_VULKAN=1` and as the CMake booleans of the same names.

## Dependencies

`cmake/dependencies.cmake` declares everything via `FetchContent`:

| Dependency | Purpose |
|---|---|
| GLFW 3.4 | Window + input |
| GLM 1.0.1 | Math (vectors, matrices, quaternions) |
| spdlog 1.16 | Logging |
| Assimp 5.3.1 | Model loading (glTF/GLB, OBJ; FBX disabled) |
| ImGui (docking) | Editor UI; backend cpp file picked from build flag |
| stb | Image loading (HDR + LDR) |
| nlohmann_json 3.11.3 | Scene JSON parsing |
| Lua 5.4.7 (walterschell wrapper) | Scripting language |
| sol2 (3.5 dev) | C++ ↔ Lua bindings |
| Jolt Physics 5.5.0 | Rigid-body physics |
| Catch2 3.8.1 | Tests (only fetched when `SONNET_BUILD_TESTS=ON`) |
| Vulkan SDK | Required when `SONNET_USE_VULKAN`; `find_package(Vulkan REQUIRED COMPONENTS glslang SPIRV-Tools)`. |
| VulkanMemoryAllocator 3.1.0 | Vulkan-only; allocator for buffers/images. |
| SPIRV-Reflect (main) | Vulkan-only; runtime descriptor + uniform reflection. |

The OpenGL loader (`glad`) lives under `vendor/` and is only added when `SONNET_USE_OPENGL` is set.

## Per-module test helper

`cmake/functions.cmake` provides `sonnet_add_module_test(<module> <sources...>)` which conditionally creates a `<module>_tests` executable linked against the module and `Catch2::Catch2WithMain`, registers it with `add_test`, and lets it include the module's `src/` directory for testing internals.

## Typical build

```bash
# OpenGL build
cmake -S . -B build -DSONNET_RENDERER_BACKEND=OpenGL
cmake --build build

# Vulkan build
cmake -S . -B build-vk -DSONNET_RENDERER_BACKEND=Vulkan
cmake --build build-vk

# Run tests
ctest --test-dir build
```

The demo executable lives at `apps/demo/` and is added unconditionally; pick the backend at configure time. The repository contains pre-configured `cmake-build-debug-opengl/` and `cmake-build-debug-vulkan/` directories used during development — you do not have to use them.
