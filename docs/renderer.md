# Renderer

The renderer is split into a backend-agnostic frontend and one of two backends (OpenGL, Vulkan). The backend boundary is the `IRendererBackend` interface in `modules/api/include/sonnet/api/render/`.

## Frontend (`modules/renderer/frontend`)

`sonnet::renderer::frontend::Renderer` (in `Renderer.h`) is the high-level API used by application code. It:

- Owns the `IRendererBackend` reference passed in at construction.
- Manages per-handle storage for meshes, shaders, material templates, textures, and render targets.
- Issues camera and light UBOs (binding points 0 and 1) every frame from the `FrameContext`.
- Walks the `RenderItem` queue, applies the matching `MaterialTemplate`'s render state (with optional `RenderOverrides`), uploads default uniforms then per-instance overrides, binds textures, and calls `backend.drawIndexed`.

The std140-laid-out UBO structs and helpers to build them from a `FrameContext` live in `UboLayouts.h`:

- `CameraUBO` (272 B): view, projection, view position, inverse view-projection, inverse projection.
- `LightsUBO` (432 B): one directional light + up to 8 point lights + count.

Frame APIs:

```cpp
renderer.beginFrame();
renderer.bindRenderTarget(rt);
renderer.render(frameContext, queue);   // upload UBOs, walk queue, draw
renderer.endFrame();                     // backend swap / submit
```

Typed-handle factories: `createMesh`, `createShader`, `createMaterial`, `createTexture`, `createRenderTarget`. Two derived helpers expose a render-target attachment as a `GPUTextureHandle` so it can be sampled by another pass:

- `colorTextureHandle(rt, i)` and `depthTextureHandle(rt)` — return a *borrowed* texture handle backed by an indirection that survives `resizeRenderTarget` (so resizing a screen-sized RT does not invalidate the handle stored on a material).

`reloadShader(handle, vertSrc, fragSrc)` recompiles in place; on failure the old shader is kept and the exception re-raised. `registerRawTexture()` lets callers wrap an externally-created `ITexture` (used for IBL cubemaps and the SSAO noise texture in the demo).

## Backend interface (`modules/api/include/sonnet/api/render/IRendererBackend.h`)

A backend implements:

- Lifecycle: `initialize`, `beginFrame`, `endFrame`.
- Framebuffer: `clear(ClearOptions)`, `bindDefaultRenderTarget`, `bindRenderTarget`, `setViewport`.
- Pipeline state: `setFillMode`, `setDepthTest/Write/Func`, `setCull`, `setBlend`, `setBlendFunc`, `setSRGB`.
- Resources: `createBuffer`, `createVertexInputState`.
- Drawing: `setUniform(location, UniformValue)`, `drawIndexed(count)`.
- Factories: `shaderCompiler()`, `textureFactory()`, `renderTargetFactory()`, `gpuMeshFactory()`.
- Identity: `traits()` returns the backend's NDC range, handedness, up axis, and whether clip-space Y is inverted (used by `core::projection::perspective` / `ortho` to build correct matrices automatically).

`UniformValue` is a `std::variant<int, float, vec2, vec3, vec4, mat4, Sampler>`, so the backend dispatches on the active alternative — frontend code never has to call type-specific entry points.

## Material model

A `MaterialTemplate` (in `api/render/Material.h`) is the shared definition: shader handle, render state, and a `UniformValueMap` of default uniform values. A `MaterialInstance` references a template by handle and stores per-object uniform overrides plus a name-keyed `GPUTextureHandle` map.

When the renderer binds a material instance for a draw, it:

1. Binds the template's shader.
2. Applies the template's `RenderState` (with `RenderOverrides` taking precedence).
3. Uploads the template's `defaultValues`.
4. Uploads the instance's overrides.
5. Binds any textures from the instance's texture map; remaining sampler uniforms in the template are bound to the texture units as configured.

## See also

- [OpenGL backend](opengl-backend.md)
- [Vulkan backend](vulkan-backend.md)
- [Render graph (demo)](render-graph.md)
