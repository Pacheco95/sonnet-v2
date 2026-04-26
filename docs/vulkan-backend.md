# Vulkan backend

`modules/renderer/vulkan/` implements `IRendererBackend` against Vulkan 1.3, with VMA for allocations, glslang for runtime GLSL → SPIR-V compilation, and SPIRV-Reflect for descriptor and uniform reflection.

The detailed implementation status — phases, architecture diagrams, what is wired, what is stubbed — lives in the standalone [`vulkan-impl.md`](../vulkan-impl.md) at the repository root. The notes below are a quick orientation; consult `vulkan-impl.md` for specifics.

## Components

| File | Role |
|---|---|
| `VkRendererBackend` | Top-level. Owns instance, device, swapchain, command context, factories, and caches. |
| `VkInstance` / `VkDevice` | Instance creation (with optional validation), physical-device picking, queue setup. |
| `VkSwapchain` | Surface formats, present modes, recreate-on-resize, frame-in-flight sync. |
| `VkCommandContext` | Per-frame command buffer + render pass scoping. |
| `VkPipelineCache` | Lazy creation of `VkPipeline` keyed by render state + shader + vertex layout + active pass. |
| `VkDescriptorManager` | Per-frame descriptor pool + set allocation; resets each frame. |
| `VkUniformRing` | Ring-allocated UBO memory for uniforms uploaded mid-frame. |
| `VkSamplerCache` | Deduplicates `VkSampler` by `SamplerDesc`. |
| `VkShaderCompiler` / `VkShader` | glslang runtime compile + SPIRV-Reflect to recover `UniformDescriptorMap`. |
| `VkTextureFactory` / `VkTexture2D` | VMA-backed image + view creation; ImGui descriptor caching for `getImGuiTextureId()`. |
| `VkRenderTarget(Factory)` | Render-pass + framebuffer creation from `RenderTargetDesc`. |
| `VkGpuBuffer` | VMA-backed buffer for vertex / index / uniform usage. |
| `VkBindState` | Frame-scoped accumulator of which shader, render state, and active pass apply to the next `drawIndexed`. |
| `VkFormatMap` | `TextureFormat` ↔ `VkFormat` translation table. |

## Traits

`traits()` returns `core::presets::Vulkan`: right-handed, Y-up, NDC Z range `[0, 1]`, **clip-space Y inverted** (so `core::projection::perspective` flips `[1][1]` automatically). Code that builds matrices through `core::projection` is correct on both backends.

## ImGui integration

The Vulkan backend exposes `imGuiInitInfo()` returning the instance/device/queue/render-pass handles imgui_impl_vulkan needs, plus `renderImGui()` to record imgui's draw data into the frame's command buffer. Under `SONNET_USE_VULKAN`, `sonnet::ui::ImGuiLayer::init` takes a `VulkanInitInfo` struct populated from this method. Under OpenGL it falls back to imgui_impl_opengl3.

## prepareForShutdown

Before the demo destroys factory-allocated resources at exit, it calls `backend.prepareForShutdown()` so the backend can `vkDeviceWaitIdle` and free per-frame work. Without it, in-flight GPU resources would be freed underneath the device.
