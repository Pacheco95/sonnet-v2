# OpenGL backend

`modules/renderer/opengl/` implements `IRendererBackend` against OpenGL 4.6 via the `glad` loader.

## Components

| File | Role |
|---|---|
| `GlRendererBackend` | Top-level backend. Owns the four factories. Implements pipeline state via direct GL calls. |
| `GlShaderCompiler` / `GlShader` | Compile `vert + frag` GLSL into a program, reflect uniform locations and types, expose `bind`. |
| `GlTextureFactory` / `GlTexture2D` | Create `GL_TEXTURE_2D` / `GL_TEXTURE_CUBE_MAP` textures from CPU data, cubemap faces, or empty (render-target attachments). Honours `SamplerDesc::depthCompare` to enable hardware shadow comparison. |
| `GlRenderTargetFactory` / `GlRenderTarget` | Create FBOs from `RenderTargetDesc`. Empty `colors` triggers `glDrawBuffer(GL_NONE) / glReadBuffer(GL_NONE)`. Implements `readPixelRGBA8` via `glReadPixels`. |
| `GlGpuBuffer` | Wraps `glCreateBuffers` + `glNamedBufferData`. Distinguishes vertex / index / uniform via `BufferType`. UBOs use `glBindBufferBase` for the std140 binding model. |
| `GlVertexInputState` | A VAO bound to a `VertexLayout`; preserves attribute layout across `bind()`. |
| `GlGpuMeshFactory` | Combines a `CPUMesh` with a freshly created vertex buffer, index buffer, and VAO into a `GpuMesh`. |

## Traits

`traits()` returns `core::presets::OpenGL`: right-handed, Y-up, NDC Z range `[-1, 1]`, no clip-space Y flip. `core::projection::perspective` and `ortho` consult these traits to pick the correct GLM helper (`*RH_NO`).

## Notes

- The backend writes directly into the default framebuffer when `bindDefaultRenderTarget()` is called.
- `setSRGB(true)` toggles `GL_FRAMEBUFFER_SRGB`; the demo enables it only when blitting to the swap-chain output.
- `setUniform` looks up the GL uniform location once at shader creation (cached in `GlShader::getUniforms()`) and dispatches by `UniformValue` alternative.
- Depth clears require depth writes to be enabled. See the project memory note: `glClearBufferfv(GL_DEPTH)` is silently dropped if `GL_DEPTH_WRITEMASK` is `GL_FALSE`.
