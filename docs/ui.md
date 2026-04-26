# UI layer

`modules/ui/` provides `ImGuiLayer`, an RAII wrapper around the ImGui (docking branch) GLFW platform backend plus the active renderer backend (imgui_impl_opengl3 or imgui_impl_vulkan).

## API

```cpp
sonnet::ui::ImGuiLayer imgui;

#if SONNET_USE_OPENGL
    imgui.init(window.handle());                       // optional GLSL version override
#elif SONNET_USE_VULKAN
    imgui.init(window.handle(), backend.imGuiInitInfo());
#endif

while (running) {
    imgui.begin();
    // ImGui::… widgets …
    imgui.end();                                       // OpenGL: also runs draw
    window.swapBuffers();
}
```

Under OpenGL, `end()` calls `ImGui::Render()` and dispatches the draw data immediately. Under Vulkan, `end()` only runs `ImGui::Render()`; the actual draw recording happens inside `VkRendererBackend::renderImGui()` which has access to the current frame's command buffer.

## Texture display

`ImTextureID` is a backend-specific opaque handle:

- OpenGL: a `GLuint` widened to `uintptr_t`.
- Vulkan: a `VkDescriptorSet` (lazily allocated via `ImGui_ImplVulkan_AddTexture` and cached) reinterpreted as `uintptr_t`.

The frontend `Renderer` exposes `imGuiTextureId(GPUTextureHandle)` which returns the right value for the active backend, so editor code (e.g. the demo's viewport panel) can call `ImGui::Image(static_cast<ImTextureID>(renderer.imGuiTextureId(tex)), size)` portably.

## sRGB note

When OpenGL `GL_FRAMEBUFFER_SRGB` is enabled, ImGui colours render too dark because ImGui assumes a linear framebuffer. `ImGuiLayer::begin()` records and disables `GL_FRAMEBUFFER_SRGB` on entry; `end()` restores the previous state. This lets the engine render the 3D scene with sRGB encoding while keeping ImGui correct.

## Demo editor

The demo (`apps/demo/EditorUI.{h,cpp}`) builds on top of `ImGuiLayer` to provide:

- **Viewport panel** — a docked image view of the scene RT with mouse picking and an in-viewport translate/rotate/scale gizmo.
- **Hierarchy panel** — tree view of `Scene::objects()`; supports selection, duplicate, and destroy.
- **Inspector panel** — edit transform, render component (mesh, material, textures), light, camera, and physics body parameters.
- **Assets panel** — lists shaders, materials, meshes, textures from the scene file.
- **Render settings panel** — controls every post-process toggle and tunable: shadow bias, exposure, bloom, SSAO, FXAA, outline, SSR, rotation speed.
- **Save scene** — round-trips the scene back to its JSON file (preserves any references the file declared).

Picking is done by rendering the scene a second time into a `RGBA8` `pickingRT` with object IDs encoded as colour, then `Renderer::readPixelRGBA8` reads back one pixel under the cursor and decodes the ID.
