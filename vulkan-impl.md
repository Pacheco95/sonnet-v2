# Vulkan Backend Implementation Status

Snapshot as of the current `main` branch (commit `5457afd`). This document covers the original plan, everything landed to date, what still needs to be done before the full `apps/demo` can run under Vulkan, and the rough cost of finishing.

---

## 1. Goals and context

The engine shipped with a single OpenGL 4.6 backend behind a clean `IRendererBackend` abstraction. The goal of this work was:

1. Add a second, fully-functional Vulkan backend implementing the same abstraction.
2. Make the backend selectable at build time (`SONNET_RENDERER_BACKEND = Auto | OpenGL | Vulkan`), auto-selecting Vulkan on macOS because Apple caps OpenGL at 4.1 and marks it deprecated.
3. Ship via MoltenVK on macOS, native drivers on Linux/Windows.
4. Update GLFW + ImGui integrations to support Vulkan alongside OpenGL.
5. Reach full visual parity with the existing OpenGL demo before shipping.

Strategy chosen (Option A in the original plan): **preserve `IRendererBackend`** with minimal additive changes; absorb Vulkan's explicit-state model inside the new `VkRendererBackend` via lazy pipeline creation, a uniform ring, per-frame descriptor pools, and a deferred-pass model. This avoids rewriting the battle-tested frontend `Renderer`, which is the most tested piece of the engine.

---

## 2. Architecture in one picture

```
                 apps/demo/main.cpp                    apps/demo/main_vk.cpp
                        |                                     |
                        v                                     v
          sonnet::renderer::frontend::Renderer          (direct backend use)
                        |
                        v
    +----- sonnet::api::render::IRendererBackend ------+
    |                                                  |
    v                                                  v
GlRendererBackend (OpenGL)                       VkRendererBackend (Vulkan)
    |                                                  |
    glad + glslang-free                         VkInstance + Device + VMA
                                                Swapchain + CommandContext
                                                PipelineCache + DescriptorManager
                                                UniformRing + SamplerCache
                                                glslang (runtime GLSL -> SPIR-V)
                                                SPIRV-Reflect (layouts + uniforms)
                                                imgui_impl_vulkan
```

Selection is compile-time via `SONNET_USE_OPENGL` / `SONNET_USE_VULKAN` compile definitions. The `BackendFactory` in `modules/renderer/frontend/` is the only `#if`-switched site in the whole codebase after Phase 0.

---

## 3. What is implemented

### 3.1 CMake / build infrastructure

- `cmake/options.cmake` — `SONNET_RENDERER_BACKEND` cache variable with values `Auto | OpenGL | Vulkan`; `Auto` resolves to Vulkan on `APPLE` and OpenGL elsewhere. Normalized into two booleans `SONNET_USE_OPENGL` and `SONNET_USE_VULKAN`, also emitted as compile definitions.
- `cmake/dependencies.cmake` — under `SONNET_USE_VULKAN`:
  - `find_package(Vulkan REQUIRED COMPONENTS glslang SPIRV-Tools)` (from the system SDK).
  - `FetchContent` of `VulkanMemoryAllocator` v3.1.0 and `SPIRV-Reflect` (main).
  - ImGui now conditionally adds `imgui_impl_opengl3.cpp` or `imgui_impl_vulkan.cpp` to the ImGui target and pulls the right link dependency (`glad` or `Vulkan::Vulkan`).
- `modules/renderer/CMakeLists.txt` — conditionally adds `opengl/` or `vulkan/` subdirectory.
- `modules/renderer/frontend/CMakeLists.txt` — conditionally links `sonnet::renderer::opengl` or `sonnet::renderer::vulkan`, and compiles the `BackendFactory.cpp` that dispatches between them.
- `vendor/CMakeLists.txt` — `glad` only added under OpenGL.
- `modules/window/CMakeLists.txt` — adds `Vulkan::Vulkan` as a PUBLIC link and defines `GLFW_INCLUDE_VULKAN` as a PUBLIC compile definition under Vulkan, so every translation unit that includes `<GLFW/glfw3.h>` pulls in `vulkan.h` (the define must live at CMake level, not in `GLFWWindow.h`, because `GLFWInputAdapter.h` pulls GLFW first and `#pragma once` suppresses the later header-level `#define`).
- `apps/demo/CMakeLists.txt` — both backends now compile the same `main.cpp` for the primary `demo` target (full deferred + IBL + shadows + post + editor pipeline). `main_vk.cpp` is preserved as a secondary minimal-triangle sanity-check binary used during bring-up; it's only a backstop, not the shipping demo path.

### 3.2 API-layer additions

All three are additive; no existing signatures were broken.

- `sonnet/api/render/IRendererBackend.h` grew one virtual: `traits() const -> const core::RendererTraits&`. `GlRendererBackend` returns `core::presets::OpenGL`; `VkRendererBackend` returns `core::presets::Vulkan`.
- `sonnet/api/render/ITexture.h` grew one virtual: `getImGuiTextureId() -> std::uintptr_t`. Returns `GLuint` widened on OpenGL. Returns 0 on Vulkan pending a proper `ImGui_ImplVulkan_AddTexture` cache (see Section 4).
- `sonnet/api/render/IRenderTarget.h` grew one virtual: `readPixelRGBA8(attachmentIx, x, y)`. OpenGL wraps `glReadPixels`. Vulkan runs a one-shot `vkCmdCopyImageToBuffer` with a transient HOST_VISIBLE staging buffer, waits on the queue, and memcpy's the result; BGRA formats are byte-swapped back to RGBA so both backends agree on byte order.
- New `sonnet/api/render/BackendCreateOptions.h` holds the options struct (validation, vsync) passed to `makeBackend(window, opts)`. Kept in the API layer so the Vulkan backend header doesn't have to include the `frontend` one (which would be circular).

### 3.3 `BackendFactory`

`modules/renderer/frontend/BackendFactory.{h,cpp}` provides the single factory function that every call site uses:

```cpp
std::unique_ptr<IRendererBackend>
makeBackend(IWindow &window, const BackendCreateOptions &opts = {});
```

Internally gated by `SONNET_USE_OPENGL` / `SONNET_USE_VULKAN`. The `BackendFactory.cpp` is the only `#if`-switched file in the whole codebase after the backend-selection decision.

### 3.4 Window module

`modules/window/GLFWWindow.{h,cpp}` now:

- Uses `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)` under Vulkan (no GL context).
- `glfwMakeContextCurrent` and `glfwSwapInterval` are OpenGL-only.
- `swapBuffers()` becomes a no-op under Vulkan — presentation happens inside `VkRendererBackend::endFrame`.
- Exposes `createVulkanSurface(VkInstance) -> VkSurfaceKHR` via `glfwCreateWindowSurface`.
- Exposes `requiredVulkanInstanceExtensions() -> std::vector<const char*>` via `glfwGetRequiredInstanceExtensions`.
- The Input adapter is unchanged — GLFW-layer input translation is backend-agnostic.

### 3.5 Vulkan instance + device + VMA

`modules/renderer/vulkan/src/VkInstance.{h,cpp}` creates the `VkInstance`:

- Always enables GLFW's required instance extensions.
- In Debug builds: enables `VK_LAYER_KHRONOS_validation` and installs a `VK_EXT_debug_utils` messenger that routes validation errors / warnings / infos to spdlog.
- On any platform that exposes `VK_KHR_portability_enumeration` (APPLE): enables it, adds `VK_KHR_get_physical_device_properties2`, and sets `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`. This is the MoltenVK path.

`modules/renderer/vulkan/src/VkDevice.{h,cpp}` picks the physical device and creates the logical one:

- Prefers discrete GPUs, scores by `maxImageDimension2D`.
- Requires a queue family that supports both graphics and presentation to the given surface.
- If the chosen physical device exposes `VK_KHR_portability_subset`, the extension is enabled at device creation (spec requirement).
- Enables `samplerAnisotropy`.
- Initializes a `VmaAllocator` with `VK_API_VERSION_1_2`.
- Provides a `runOneShot(std::function<void(VkCommandBuffer)>)` helper that allocates a transient primary command buffer from an internal `VK_COMMAND_POOL_CREATE_TRANSIENT_BIT` pool, records, submits, `vkQueueWaitIdle`s, and frees. Used by resource constructors (buffer/texture staging uploads, render-target layout transitions) where latency doesn't matter.

### 3.6 Swapchain + default render pass

`modules/renderer/vulkan/src/VkSwapchain.{h,cpp}` owns:

- `VkSwapchainKHR` with an sRGB color format (`VK_FORMAT_B8G8R8A8_SRGB` or fallback), `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`, `VK_PRESENT_MODE_FIFO_KHR` (universal), 3 images.
- Per-image `VkImageView`s.
- A shared `VkImage` depth attachment in `VK_FORMAT_D32_SFLOAT` (or fallback), VMA-allocated, with its own view.
- The "default" `VkRenderPass` used by the swapchain — one color + one depth attachment, `LOAD_OP_CLEAR` on both, `STORE_OP_STORE` for color, `STORE_OP_DONT_CARE` for depth, color final layout `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`.
- One `VkFramebuffer` per swapchain image.
- `recreate(w, h)` tears down and rebuilds everything after a resize or `VK_ERROR_OUT_OF_DATE_KHR`, with a `vkDeviceWaitIdle` first (coarse but bulletproof for v1).

### 3.7 Command context + frames in flight

`modules/renderer/vulkan/src/VkCommandContext.{h,cpp}` owns two `FrameData` slots (`kFramesInFlight = 2`), each with:

- Its own `VkCommandPool` (reset at frame begin).
- Primary `VkCommandBuffer`.
- `VkFence inFlightFence` — CPU waits on this at the top of every frame before reusing the slot.
- `VkSemaphore imageAvailable` — signaled by `vkAcquireNextImageKHR`, waited on by the queue submit.
- `VkSemaphore renderFinished` — signaled by the queue submit, waited on by the present.

`beginFrame()` waits the fence, resets it, resets the pool, and returns the slot. `advance()` rotates to the next slot after submit+present complete.

### 3.8 GPU resources (Phase 2)

All the `IRendererBackend` factory methods are wired to real implementations except the two stubs called out in Section 4.

- `VkGpuBuffer` — VMA-backed. For `Vertex` / `Index`: device-local, uploaded once via a host-visible staging buffer + `vkCmdCopyBuffer` inside `Device::runOneShot`. `update()` throws on static buffers. For `Uniform`: host-coherent, persistently mapped via `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT`; `update()` memcpy's directly. `bind()` writes to `BindState.currentVertex` / `.currentIndex`; `bindBase(n)` writes to `BindState.ubos[n]` + `uboSizes[n]`.
- `VkTexture2D` — VMA image with staging upload and mipmap generation via `vkCmdBlitImage` loop. Samplers cached on the backend by `SamplerDesc`. Layout lifecycle: UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY (with mipmap blits producing TRANSFER_SRC transitions per level). Allocate-only (render-target) path transitions straight to SHADER_READ_ONLY or DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
- `VkTextureFactory` — routes to `VkTexture2D` for `CPUTextureBuffer` and for allocate-only. `CubeMapFaces` throws (Section 4).
- `VkRenderTarget` — owns `std::unique_ptr<VkTexture2D>` color + optional depth attachments, plus its own `VkRenderPass` (initial+final layout = SHADER_READ_ONLY_OPTIMAL, clear-to-store on color, clear-to-dont-care on depth) and `VkFramebuffer`. Subpass dependencies handle the outside-the-pass layout transitions. Not yet plugged into `bindRenderTarget` (Section 4).
- `VkRenderTargetFactory` — direct passthrough to the constructor.
- `VkGpuMeshFactory` — identical in shape to the OpenGL one: uses `backend.createBuffer` + `createVertexInputState` internally.
- `VkVertexInputState` — pure data holder. Converts the engine's typed `KnownAttributeSet` into `VkVertexInputBindingDescription` + `VkVertexInputAttributeDescription[]` at construction time; `bind()` writes to `BindState.currentVertexInput`; no actual binding happens (Vulkan bakes vertex input into `VkPipeline`).
- `VkShader` — two `VkShaderModule`s, reflected descriptor-set layouts + push-constant ranges, combined `VkPipelineLayout`, and a `ShaderReflection` struct carrying the reflected `UniformDescriptorMap` + a parallel `entries[]` array that classifies each uniform by kind (`PushConstant`, `MaterialSampler`, `PerDrawUbo`) with byte offset/size, set/binding, and stage flags.
- `VkShaderCompiler` — one instance per backend; calls `glslang_initialize_process()` in ctor and `glslang_finalize_process()` in dtor. Each `operator()(vertSrc, fragSrc)`:
  1. Runs glslang twice (vertex + fragment) with `GLSLANG_CLIENT_VULKAN`, `GLSLANG_TARGET_VULKAN_1_2`, `GLSLANG_TARGET_SPV_1_5`.
  2. Injects a preamble via `glslang_shader_set_preamble`: `#define SET(n,b) set = n, binding = b\n`. `VULKAN` is already auto-defined by glslang in Vulkan client mode — redefining it produces a "Macro redefined; different substitutions" error.
  3. Runs SPIRV-Reflect on each stage module and unions results: descriptor bindings (per-set), push-constant ranges (with stage flags), vertex-input attributes, push-constant block members, set=1 sampler names, and set=2 PerDraw UBO block members.
  4. Hands everything to `VkShader`'s constructor.

### 3.9 Pipeline cache

`modules/renderer/vulkan/src/VkPipelineCache.{h,cpp}` (class `PipelineCache` — no `Vk` prefix to avoid colliding with `::VkPipelineCache`).

Key:
```cpp
struct Key {
    const VkShader           *shader;
    const VkVertexInputState *vis;
    api::render::RenderState  state;          // existing struct, hashable
    VkRenderPass              renderPass;
    std::uint32_t             colorAttachmentCount;
    bool                      hasDepth;
};
```

Backed by `std::unordered_map` plus an internal `::VkPipelineCache` for driver-side warm-start. Viewport and scissor are declared dynamic, so pipelines survive window resize without re-creation. Blend factors are derived from `RenderState.blend` via a fixed mapping (`Opaque` / `Alpha` / `Additive`). Polygon mode, cull mode, compare op all map directly. `VK_FRONT_FACE_COUNTER_CLOCKWISE`.

### 3.10 Descriptor manager

`modules/renderer/vulkan/src/VkDescriptorManager.{h,cpp}` (class `DescriptorManager`).

- Two `VkDescriptorPool`s (one per frame-in-flight), each sized `maxSets=512`, `UNIFORM_BUFFER=1024`, `COMBINED_IMAGE_SAMPLER=4096`. Reset wholesale at `beginFrame(frameIx)`.
- `allocateFrameSet0(shader)` — looks at `shader.setLayouts()[0]` and the reflected set-0 bindings; for every UBO binding, writes `BindState.ubos[b.binding]` + size into a `VkDescriptorBufferInfo`, emits one `VkWriteDescriptorSet` per binding, runs `vkUpdateDescriptorSets`, returns the set.
- `allocateMaterialSet1(shader)` — same pattern with `VkDescriptorImageInfo` (view + sampler + `SHADER_READ_ONLY_OPTIMAL`). Reads `BindState.materialTextures[b.binding]` for each expected binding. Currently ignores `descriptorCount > 1` (Section 4).
- `allocatePerDrawSet2(shader, buffer, offset, range)` — writes one UBO binding pointing to `(buffer, offset, range)`. Binding target is always set=2, binding=0 by convention.

### 3.11 Uniform ring

`modules/renderer/vulkan/src/VkUniformRing.{h,cpp}` (class `UniformRing`).

- One large VMA-allocated HOST_VISIBLE + HOST_COHERENT + persistently-mapped `VkBuffer` split into `kFramesInFlight` slices (2 MiB per frame).
- Queries `minUniformBufferOffsetAlignment` from the physical device and aligns every allocation up to that boundary.
- `beginFrame(f)` resets slice `f`'s cursor.
- `allocate(size)` returns `{mapped ptr, byte offset from start of whole buffer}` — both of which `drawIndexed` writes + hands to `DescriptorManager::allocatePerDrawSet2`.

### 3.12 Material texture binding (set=1)

`BindState` extended with `std::array<const VkTexture2D*, 16> materialTextures`. `VkTexture2D::bind(slot)` writes into that array. `DescriptorManager::allocateMaterialSet1` reads it when building the per-material descriptor set. `setUniform(loc, Sampler{n})` is a no-op under Vulkan — the binding is determined by which slot the engine's `Renderer::bindMaterial` passed to `texture->bind`, not by the value held in `UniformValue`.

### 3.13 Push constants + PerDraw UBO routing

`VkRendererBackend::setUniform(location, value)`:

1. Look up `ShaderUniformEntry` at `shader.uniformEntry(location)`. If missing, return.
2. Switch on `entry->kind`:
   - **`PushConstant`** — memcpy the bytes into `BindState.pushConstantStaging[offset .. offset+size]`, advance `pushConstantDirtyEnd` HWM.
   - **`PerDrawUbo`** — memcpy into `BindState.perDrawStaging[offset .. offset+size]`, advance `perDrawDirtyEnd` HWM.
   - **`MaterialSampler`** — no-op.
   - **`Unknown`** — no-op.

Staging sizes: 128 bytes for push (Vulkan's guaranteed minimum), 16 KiB for PerDraw (generous — covers 128-bone skinning arrays). Both HWMs are reset in `BindState::clearDrawScopedState()` after each `drawIndexed`.

`drawIndexed(indexCount)`:

1. Bail cheaply if no shader / vertex-input / vertex-buffer / index-buffer is bound.
2. Look up the pipeline in `PipelineCache` via `(shader*, renderState, vis*, renderPass, colorCount, hasDepth)`; create if miss.
3. `vkCmdBindPipeline`.
4. Bind set 0 (frame UBOs) if the shader declares one — `allocateFrameSet0` returns `VK_NULL_HANDLE` otherwise.
5. Bind set 1 (material samplers) same way.
6. If the shader declares a PerDraw UBO: allocate `perDrawUboSize` bytes from the `UniformRing`, memcpy `perDrawStaging`, allocate a set-2 descriptor set pointing at `(ring.buffer, ring.offset, perDrawUboSize)`, bind.
7. `vkCmdPushConstants` with the HWM-sized slice of `pushConstantStaging`, stage flags unioned across all reflected push ranges.
8. `vkCmdBindVertexBuffers` with `BindState.currentVertex`, `vkCmdBindIndexBuffer` with `BindState.currentIndex` and `VK_INDEX_TYPE_UINT32`.
9. `vkCmdDrawIndexed`.
10. `BindState.clearDrawScopedState()` — zero the material-texture array and reset both dirty HWMs so the next draw starts clean.

### 3.14 ImGui integration

`modules/ui/src/ImGuiLayer.cpp` conditionally uses `imgui_impl_opengl3` or `imgui_impl_vulkan`. Under Vulkan:

- `ImGuiLayer::init(GLFWwindow*, VulkanInitInfo)` calls `ImGui_ImplGlfw_InitForVulkan` + `ImGui_ImplVulkan_Init`. Uses the docking-branch `ImGui_ImplVulkan_PipelineInfo` API (post-2025/09/26) — `RenderPass`, `Subpass`, `MSAASamples` now live on `PipelineInfoMain`, not the top-level init info.
- `ImGuiLayer::begin()` calls `ImGui_ImplVulkan_NewFrame` + `ImGui_ImplGlfw_NewFrame` + `ImGui::NewFrame`.
- `ImGuiLayer::end()` just calls `ImGui::Render`.
- `VkRendererBackend::renderImGui()` picks up `ImGui::GetDrawData()` and records it into the current command buffer via `ImGui_ImplVulkan_RenderDrawData`, layered inside the main render pass so ImGui overlays the 3D output.

`VkRendererBackend` owns a dedicated `VkDescriptorPool` sized for `COMBINED_IMAGE_SAMPLER * 1024, maxSets = 1024` with `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` set, since `imgui_impl_vulkan` allocates+frees one descriptor per `ImGui_ImplVulkan_AddTexture` call.

### 3.15 Projection corrections (`RendererTraits`)

`modules/core/include/sonnet/core/RendererTraits.h`:

- `presets::OpenGL`, `presets::Vulkan`, `presets::DirectX` structs already existed; added `presets::Active()` as a compile-time alias for the selected one.
- New `sonnet::core::projection::perspective(fovY, aspect, near, far)` and `::ortho(l, r, b, t, n, f)` helpers. Each inspects `presets::Active()`:
  - If `ndcZMin == 0.0f`, picks `glm::perspectiveRH_ZO` / `orthoRH_ZO`; otherwise `..._NO`.
  - If `clipSpaceYInverted`, multiplies row 1 by -1 (Y-flip for Vulkan's top-left origin).
- `modules/world/.../CameraComponent.h::projectionMatrix` and `apps/demo/ShadowMaps.cpp` (both cascade-ortho and point-light-perspective projections) go through these helpers. IBL's cube-face projection is still raw `glm::perspective` because the IBL.h file is still OpenGL-only (tracked under Phase 7b).

### 3.16 Shader rewrites (22 of ~30)

Every shader compile passes through one of two compilers:

- `GlShaderCompiler` — splices a preamble (`#define SET(n,b) binding = b\n`) into the source right after the `#version` line, then hands to `glShaderSource` + `glCompileShader`.
- `VkShaderCompiler` — calls `glslang_shader_set_preamble` with `#define SET(n,b) set = n, binding = b\n`. `VULKAN` is auto-defined by glslang in Vulkan-client mode, so shaders use `#ifdef VULKAN` directly.

The unified shader pattern is then:

```glsl
#version 460 core

layout(std140, SET(0,0)) uniform CameraUBO { ... };
layout(SET(1,0)) uniform sampler2D uAlbedo;

#ifdef VULKAN
layout(push_constant) uniform Push { mat4 uModel; float uMetallic; ... } pc;
#define uModel     pc.uModel
#define uMetallic  pc.uMetallic
#else
uniform mat4  uModel;
uniform float uMetallic;
#endif

// PerDraw UBO for data > 128 B that's not frame-static:
#ifdef VULKAN
layout(std140, SET(2,0)) uniform PerDraw { mat4 uBoneMatrices[128]; } pd;
#define uBoneMatrices pd.uBoneMatrices
#else
uniform mat4 uBoneMatrices[128];
#endif
```

Both backends compile the same source. OpenGL gets plain default uniforms (set via `glUniform1i` / `glUniformMatrix4fv`). Vulkan gets push constants for per-draw scalars ≤ 128 B, set=2 PerDraw UBO for anything larger (bone arrays, CSM light-space matrices, SSAO kernel).

**Shaders already ported** (22):

| Shader | Pattern used |
|---|---|
| `emissive.vert`, `emissive.frag` | Shared push block (uModel + uEmissiveColor + uEmissiveStrength) |
| `sky.vert`, `sky.frag` | set=0 CameraUBO + set=1 samplers; no per-draw |
| `tonemap.frag` | set=1 samplers + push (3 floats) |
| `fxaa.frag` | set=1 samplers + push (vec2 uTexelSize) |
| `bloom_bright.frag`, `bloom_blur.frag` | set=1 sampler + push (threshold / horizontal int) |
| `ssao.frag`, `ssao_blur.frag`, `ssao_show.frag` | set=0 Camera + set=1 samplers + PerDraw UBO with 64-element vec3 kernel (1 KiB std140) |
| `shadow.vert` | PerDraw UBO with uView/uProjection/uModel (3 mat4 = 192 B — too big for push) |
| `picking.vert`, `picking.frag` | Shared push (uModel + uPickColor) |
| `gbuffer.vert`, `gbuffer.frag`, `gbuffer_emissive.frag` | Shared 116-B push (uModel + 6 material scalars); set=1 four material textures |
| `gbuffer_skinned.vert`, `outline_mask_skinned.vert` | PerDraw UBO holds 128-mat4 bone palette (8 KiB) + shared push block |
| `ssr.frag` | set=0 Camera + set=1 G-buffer samplers + push (28 B of tuning) |
| `outline.frag` | set=1 sampler + push (vec3 color) |
| `deferred_lighting.frag` | set=0 Camera + Lights; set=1 fifteen bindings (incl. `sampler2DShadow[3]` + `samplerCube[4]`); PerDraw UBO holds 3 CSM light-space mats + 3 split depths (240 B); push holds 5 scalars (20 B) |

**Shaders not yet ported** (8): `point_shadow.vert`, `point_shadow.frag`, and all six IBL shaders (`ibl/equirect_to_cube.frag`, `irradiance.frag`, `prefilter.frag`, `brdf_lut.vert`, `brdf_lut.frag`, `capture.vert`). All use geometry shaders to emit to six cubemap faces in one pass, which Vulkan/MoltenVK don't easily support — they need a rewrite to six per-face passes (Section 4.4).

Trivial shaders that required no changes: `tonemap.vert`, `fxaa.vert`, `ssao.vert`, `outline.vert` (pure pass-through, no uniforms) and `outline_mask.frag` (writes `vec4(1.0)` — no uniforms) and `shadow.frag` (empty `main`).

### 3.17 Demo-side refactors

- `apps/demo/RenderGraph.{h,cpp}` — takes `IRendererBackend&` (was `GlRendererBackend&`). `glDepthMask(GL_TRUE)` → `backend.setDepthWrite(true)`. Drops `<glad/glad.h>`.
- `apps/demo/PostProcess.{h,cpp}` — takes `IRendererBackend&`. Drops glad include.
- `apps/demo/EditorUI.{h,cpp}` — takes `IRendererBackend&`. Picking pass now calls `m_renderer.readPixelRGBA8(p.pickingRT, 0, px, py)` instead of `glReadPixels`. `viewportTexId` is `ImTextureID` (was `GLuint`), fed from `renderer.imGuiTextureId(...)`.
- `apps/demo/ShadowMaps.{h,cpp}` — takes `IRendererBackend&`. Still contains the raw-GL cubemap FBO for point-light shadows (Section 4.6).
- `apps/demo/main.cpp` — no longer needs the `GlRendererBackend&` downcast; stores `auto &backend = *backendPtr`. SSAO noise texture creation now goes through `renderer.createTexture` with an RGBA32F `CPUTextureBuffer` instead of raw `glGenTextures`. The pre-ImGui `glClearColor` + `glClear` is replaced by `backend.clear({.colors={{0, ...}}})`. Most importantly, `<glad/glad.h>` is no longer included by main.cpp.

### 3.18 `main_vk.cpp` — Vulkan bring-up sanity binary

The full `apps/demo/main.cpp` now builds and runs on both backends (since
commit `9ebe04d`). `apps/demo/main_vk.cpp` was the original Vulkan-only
self-contained triangle demo and is kept around as a minimal sanity-check
executable for bring-up: inline Vulkan-GLSL shader with
`layout(push_constant) Push { mat4 uModel; }` + `layout(set=2, binding=0)
PerDraw { vec4 uTint; }`; vertex + index buffers created through the
factory; ImGui overlay via the same `ImGuiLayer` the OpenGL demo uses; FPS
counter + escape-to-close. Runs clean on MoltenVK with portability + MVK
ICD. Useful when the full demo is broken and you want to confirm the
backend-itself still works.

### 3.19 Runtime fixes for the full-demo path on Vulkan

Several non-trivial runtime fixes landed after the full demo started
building under Vulkan; they belong in this snapshot because they encode
load-bearing assumptions:

- `9b91dc8` — `endFrame` now respects an in-flight frame; mip transitions
  use the right access masks; `RGB8` is widened to `RGBA8` on upload (MoltenVK
  doesn't expose `R8G8B8_*`); render-buffer depth attachments work on Vulkan.
- `9b8cd0a` — depth layouts unified to `DEPTH_STENCIL_READ_ONLY_OPTIMAL`
  when sampled and `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` when written; push
  constant ranges declared with the exact stage flags expected by validation;
  default texture installed for unbound material slots; viewport flipped
  for Vulkan's top-left origin.
- `45d94be` — `Renderer::beginFrame`/`endFrame` are reference-counted so
  IBL bake (which begins/ends inner frames during init while the outer demo
  frame is still pending) doesn't double-present and cause flicker.
- `50dacf8` — **Camera/Lights UBO race fix**: when a UBO `update()` call
  happens *inside* an active frame (e.g. mid-pass), Vulkan can no longer use
  the host-mapped path because the GPU is still reading the previous
  contents. The Vulkan `IGpuBuffer::update()` now routes to
  `vkCmdUpdateBuffer` recorded into the current frame's command buffer
  whenever a frame is pending, and falls back to the host-mapped write
  outside a frame. **Anyone touching the renderer needs to know this** —
  small per-frame UBO writes look free under OpenGL but cost a command
  buffer recording on Vulkan.

---

## 4. What is missing

Estimates are "focused hours of agent work" — roughly 1/3 of wall-clock when interleaved with validation checks and rebuilds.

### 4.1 `bindRenderTarget` — deferred render-pass model — DONE (commit b2eb106)

Implemented per the plan: `bindRenderTarget` / `bindDefaultRenderTarget` queue
the target as `m_pending`, `clear()` folds into the pending clear values, and
`ensurePassActive()` records `vkCmdBeginRenderPass` lazily on the first
`drawIndexed` or `renderImGui()` call. `endFrame` defensively folds to the
default RT if the user didn't bind it. Pipeline-cache key already includes
`renderPassCompatHash`. Subpass dependencies on `VkRenderTarget` cover the
read-after-write case for sampled-from-color attachments.

### 4.2 Cubemap texture support — DONE (commit 4dc13fa)

Both backends now wire `ITextureFactory::create(desc, sampler, CubeMapFaces&)`
to a real implementation. Vulkan creates a single `VkImage` with `arrayLayers=6`
and `CUBE_COMPATIBLE_BIT`, uploads all six faces from one staging buffer with a
six-region `vkCmdCopyBufferToImage`, and creates a `VK_IMAGE_VIEW_TYPE_CUBE`
view. `generateMipmaps` now uses `m_layerCount` so cubemap mip chains stay
consistent. OpenGL similarly tracks a `m_target` that toggles `GL_TEXTURE_2D` /
`GL_TEXTURE_CUBE_MAP`. Allocate-only ctor accepts `desc.type = CubeMap` too,
for cubemap RTs (commit 45ca6a9).

### 4.3 Cubemap-layered render targets — DONE (commit 9644fce)

`RenderTargetDesc` gained `isCubemap` and `mipLevels` fields. When isCubemap,
color (and depth-as-texture) is allocated as a cubemap and the RT pre-builds
mipLevels × 6 framebuffers/views. `IRenderTarget::selectCubemapFace(face,
mip)` chooses which one bind() reads. `Renderer::selectCubemapFace` exposes
this through the frontend handle. Both backends implemented.

### 4.4 `point_shadow` + IBL shader rewrites (6-face passes) — DONE

`point_shadow.{vert,frag}` ported in commit `0e2ade2` — they were already
single-pass (no geometry-shader trick), so the rewrite was just SET()/push
convention. All six IBL shaders (`ibl/equirect_to_cube.frag`,
`irradiance.frag`, `prefilter.frag`, `brdf_lut.{vert,frag}`, `capture.vert`)
are likewise single-pass; the SET()/push port landed alongside the IBL.h
rewrite (commit `4158189`, item 4.7).

### 4.5 Descriptor arrays (`sampler2DShadow uShadowMaps[3]`) — DONE (commit edfe9ee)

`reflectMaterialSamplers` now expands array bindings into per-element entries
(`"uShadowMaps[0]"`, `"[1]"`, `"[2]"`) sharing the same `(set, binding)` but
with distinct `arrayElement` values. `allocateMaterialSet1` writes
`descriptorCount` image infos per binding, drawn from
`materialTextures[binding + i]`.

The slot/binding decoupling problem (deferred-lighting binds aren't sequential
matches to slot order) is solved by adding a slot-keyed staging table to
`BindState`: `texture->bind(slot)` records into `texturesBySlot[slot]`, and
`setUniform` for a `MaterialSampler` reads that and stores at
`materialTextures[binding + arrayElement]`. `kMaxMaterialTextures` bumped
16→32 to fit deferred-lighting's binding=14.

### 4.6 `ShadowMaps.cpp` point-shadow refactor — DONE (commit ed5ff58)

ShadowMaps no longer touches raw GL. Each shadow-casting point light has a
cubemap RT (R32F color + depth renderbuffer); the per-face loop uses
`renderer.selectCubemapFace` + `renderer.bindRenderTarget`. `glDepthMask` is
now `setDepthWrite`. The point_shadow shader was already a normal vert+frag
pair (no geometry shader); the OpenGL-only geometry-shader fan-out path
described in the original plan never existed in this branch.

### 4.7 `IBL.h` rewrite — DONE (commit 4158189)

Rewritten end-to-end on top of the engine abstractions: equirect HDR uploads
through `ITextureFactory`, cubemap RTs via `isCubemap=true`, per-face render
loop via `selectCubemapFace` + `Renderer::render()`. Drops RawGLTexture2D /
RawGLCubeMap and the glad include. The cubemap RTs use `RenderBufferDesc`
depth, which now has Vulkan parity (commit 9b91dc8).

### 4.8 `getImGuiTextureId()` for Vulkan — DONE (commit 8e764bb)

`VkTexture2D::getImGuiTextureId()` lazily allocates a `VkDescriptorSet` via
`ImGui_ImplVulkan_AddTexture(sampler, view, SHADER_READ_ONLY_OPTIMAL)` on first
call and caches it for the texture's lifetime. The destructor calls
`ImGui_ImplVulkan_RemoveTexture` to free it back to the pool (which was already
created with `FREE_DESCRIPTOR_SET_BIT`).

### 4.9 MoltenVK polish (Phase 5) — PARTIAL

The portability-enumeration path is live; MoltenVK loads, presents, and runs
`main_vk.cpp` cleanly. Validation-layer Debug build runs zero validation
errors after this session's work. Commit 1fbc41f added an explicit query of
`VkPhysicalDevicePortabilitySubsetFeaturesKHR` with warnings for
mutableComparisonSamplers + imageViewFormatReinterpretation.

Still outstanding:
- Validation-layer best-practices layer soak over 10+ minutes of the full demo.
- HiDPI verification on retina (`glfwGetFramebufferSize` and `currentExtent`
  parity).
- README note on `VK_ICD_FILENAMES` if not using the `.pkg` SDK installer.

### 4.10 Phase 8 hardening — PARTIAL

- **Hot-reload pipeline-cache invalidation — DONE**:
  `IRendererBackend::invalidatePipelinesForShader(IShader&)` is a default
  no-op virtual; `VkRendererBackend` overrides it to `vkDeviceWaitIdle()`
  + `PipelineCache::invalidateForShader(VkShader*)`, which destroys every
  cached pipeline whose key references the old shader. `Renderer::reloadShader`
  calls it with the still-live old `IShader&` immediately before moving the
  recompiled module into the slot. Unit-tested via `MockRendererBackend`
  recording the invalidate calls.
- **Outstanding**:
  - Stress test: 1000+ drawIndexed per frame, no `OUT_OF_POOL_MEMORY`, no VMA fragmentation warnings.
  - Long soak: 10-minute run, no leaked ImGui textures, no leaked descriptor sets.
  - Optional: persist the driver-side `::VkPipelineCache` to disk on shutdown, load on startup. Meaningful cold-start-time improvement.

**Effort:** medium — ~3–5 hours for the remaining items.

---

## 5. Commits on the feature chain

In chronological order on `main`:

| # | Hash | What |
|---|---|---|
| 1 | `e9555e0` | Vulkan backend skeleton + `SONNET_RENDERER_BACKEND` + BackendFactory + hello-clear |
| 2 | `6e085df` | Phase 2: all Vulkan resource classes (buffers, textures, shaders via glslang, render targets, vertex input, meshes) |
| 3 | `3e4ff24` | Phase 3a: SPIRV-Reflect -> VkDescriptorSetLayout + VkPipelineLayout |
| 4 | `31351e4` | Phase 3b + 3c: PipelineCache + DescriptorManager + working drawIndexed |
| 5 | `87b0b71` | Phase 3d + 3e: push constants + material sets + triangle demo |
| 6 | `04007bf` | Phase 6: projection corrections via `RendererTraits` |
| 7 | `0084fb9` | Phase 4: ImGui on Vulkan |
| 8 | `67f6cdc` | Phase 7a: demo pass classes take `IRendererBackend&` |
| 9 | `166fd89` | `IRenderTarget::readPixelRGBA8` abstraction + GL impl |
| 10 | `5368241` | Phase 3f: PerDraw UBO routing (set=2) |
| 11 | `b50617a` | `VkRenderTarget::readPixelRGBA8` Vulkan impl |
| 12 | `4acba60` | `SET(n,b)` preamble infra + first emissive rewrite |
| 13 | `07f3909` | Dropped halves of preamble infra picked up |
| 14 | `cef6ce6` | Port post-process shaders to SET()/push |
| 15 | `0fb0270` | Port gbuffer, shadow, picking, outline shaders |
| 16 | `a13a284` | Port skinned + SSR shaders |
| 17 | `e807000` | Port `deferred_lighting.frag` + fix VULKAN-macro-redefined |
| 18 | `267afd4` | main.cpp: SSAO noise via `ITextureFactory` + drop glad include |
| 19 | `b2eb106` | Phase 4.1: deferred render-pass model in `bindRenderTarget` |
| 20 | `edfe9ee` | Phase 4.5: descriptor arrays in material set=1 |
| 21 | `8e764bb` | Phase 4.8: VkTexture2D::getImGuiTextureId via ImGui_ImplVulkan_AddTexture |
| 22 | `4dc13fa` | Phase 4.2: cubemap texture upload (both backends) |
| 23 | `45ca6a9` | Allocate-only cubemap RT path on Vulkan |
| 24 | `9644fce` | Phase 4.3: cubemap render targets with per-face attachment |
| 25 | `ed5ff58` | Phase 4.6: ShadowMaps point-shadow refactor onto cubemap RT |
| 26 | `1fbc41f` | Phase 4.9 (partial): MoltenVK portability_subset features query |
| 27 | `0e2ade2` | Phase 4.4 (partial): point_shadow shaders to SET()/push convention |
| 28 | `c7b30ba` | RG16F + RGB16F formats (prerequisite for IBL refactor) |
| 29 | `8f707cc` | Drop glReadPixels in EditorUI; use Renderer::readPixelRGBA8 |
| 30 | `4158189` | Phase 4.7: rewrite IBL precompute on engine abstractions |
| 31 | `9ebe04d` | Compile full demo on both backends (CMakeLists, ImGui init, shader locations) |
| 32 | `9b91dc8` | Vulkan runtime fixes: endFrame frame-pending, mip transitions, RBO depth, RGB8 widening |
| 33 | `ff691d3` | Doc: log IBL refactor + remaining Vulkan runtime work |
| 34 | `9b8cd0a` | Vulkan validation fixes: depth layouts, push ranges, default texture, viewport |
| 35 | `45d94be` | Fix Vulkan demo flicker: refcount renderer.beginFrame/endFrame |
| 36 | `50dacf8` | Fix CameraUBO/LightsUBO race: route in-frame writes via vkCmdUpdateBuffer |
| 37 | (this commit) | Phase 4.10 (partial): hot-reload pipeline-cache invalidation |

---

## 6. Cost estimate to finish

Very rough, see the conversation log for reasoning. Ranges assume Opus-4.x pricing; Sonnet would be ~5× cheaper for mechanical work.

All of 4.1–4.8 are landed. The full demo runs on both backends. The
hot-reload correctness fix from 4.10 is in. What's left is operational
polish only:

| Bucket | Remaining items | Hours | Opus USD | Mixed Opus + Sonnet |
|---|---|---|---|---|
| MoltenVK polish | 4.9 docs/soak/HiDPI | ~2 | $50–$150 | $20–$60 |
| Hardening | 4.10 stress + soak tests, optional disk pipeline cache | ~3 | $80–$200 | $30–$80 |
| **Total** | | **~5** | **$130–$350** | **$50–$140** |

---

## 7. How to run what exists

```bash
# OpenGL (full demo: shadows, IBL, SSAO, bloom, SSR, FXAA, outline, editor UI)
cmake -DSONNET_RENDERER_BACKEND=OpenGL -B build-gl
cmake --build build-gl -j8
./build-gl/apps/demo/demo

# Vulkan (full demo, same source set as OpenGL)
cmake -DSONNET_RENDERER_BACKEND=Vulkan -B build-vk
cmake --build build-vk -j8
./build-vk/apps/demo/demo

# Auto (picks Vulkan on macOS, OpenGL elsewhere)
cmake -B build
cmake --build build -j8
./build/apps/demo/demo

# Tests (one executable per module — 11 total — on both backends)
(cd build-gl && ctest)
(cd build-vk && ctest)
```

Both `ctest` runs pass 11/11 green.

---

## 8. Open design questions for the next session

1. **`bindRenderTarget` implementation order**: does the deferred-pass model belong inside `VkRendererBackend` or inside a new `VkRenderPassManager`? The former is simpler; the latter lets us cache render-pass compatibility keys separately from the RT objects, which matters when the pipeline cache keys on `renderPassCompatHash`.
2. **Descriptor-array slot convention**: do we adopt "slot = binding + arrayElement" (needs Renderer to iterate in binding order — fragile) or a reflection-driven lookup (touches `Renderer::bindMaterial` with a small backend-aware branch)? The latter is cleaner.
3. **IBL layered rendering**: do we commit to pure six-per-face passes for simplicity, or add optional `VK_KHR_multiview` support later for perf? Six-per-face works on every Vulkan profile; multiview is optional.
4. **Pipeline cache persistence**: worth the ~30 lines of code for meaningfully faster cold-start? Only if the full demo pipeline count grows large enough to matter — empirically ~40 pipelines once the full demo is running, which takes ~80 ms on a cold driver cache. Probably not worth it for this project.

---

*Document maintained by the Vulkan backend feature chain. Last updated alongside commit `5457afd` + the hot-reload pipeline-cache invalidation fix that follows it.*
