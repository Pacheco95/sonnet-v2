# Image-based lighting (demo)

`apps/demo/IBL.h` is a header-only utility that bakes the three GPU resources needed for split-sum image-based PBR lighting from a single equirectangular HDR environment map. It runs once at startup and registers each result with the engine `Renderer` as a `GPUTextureHandle`.

> **Note:** Currently OpenGL-only. The implementation talks to the GL backend directly through glad to capture cubemap faces; the equivalent Vulkan path is part of the [Vulkan backend](vulkan-backend.md) follow-up work tracked in `vulkan-impl.md`.

## Inputs

```cpp
IBLMaps ibl = buildIBL(renderer, "kloppenheim_06_1k.hdr",
                       DEMO_ASSETS_DIR "/shaders");
```

`buildIBL` expects the IBL shader pair files under `<shaderDir>/ibl/`:

```
ibl/capture.vert            — shared vertex stage for cubemap capture
ibl/equirect_to_cube.frag   — sample equirectangular HDR per cube face
ibl/irradiance.frag         — diffuse irradiance convolution
ibl/prefilter.frag          — GGX importance-sampled specular pre-filter
ibl/brdf_lut.vert/.frag     — split-sum BRDF look-up table
```

## Outputs

`IBLMaps` carries both the raw GL ids (so `destroyIBL` could free them on shutdown) and engine-side handles registered via `Renderer::registerRawTexture(std::make_unique<RawGLTexture2D|RawGLCubeMap>(id))`:

| Field | Format / Size | Use |
|---|---|---|
| `equirectHandle`     | 2D RGB16F (source size) | Skybox sampling. |
| `irradianceHandle`   | Cubemap 32×32 RGB16F | Diffuse IBL term. |
| `prefilteredHandle`  | Cubemap 128×128 RGB16F, **5 mips** | Specular IBL term; mip level encodes roughness. |
| `brdfLUTHandle`      | 2D RG16F 512×512 | Split-sum F-term lookup. |

## Pipeline

1. **HDR load** — `stbi_loadf` decodes the equirectangular HDR; uploaded to a `GL_RGB16F` 2D texture.
2. **Equirect → cube** — render a unit cube from the origin into a 512×512 RGB16F cubemap, six faces, sampling the equirect map by direction. Mipmaps are generated for the env cubemap so the prefilter pass can sample at varied quality.
3. **Irradiance** — render the cube again into a 32×32 cubemap; the fragment shader integrates radiance over the hemisphere around each direction.
4. **Pre-filtered specular** — render five mip levels of a 128×128 cubemap, varying roughness from 0 (mip 0) to 1 (mip 4). Each mip is a GGX importance-sampled convolution of the env cubemap.
5. **BRDF LUT** — render a fullscreen quad once into a 512×512 RG16F texture; the fragment shader integrates `(NdotV, roughness) → (F0 scale, F0 bias)`.

The function preserves the OpenGL state it touches (`GL_FRAMEBUFFER_BINDING`, viewport, `GL_DEPTH_TEST`) and restores them before returning, so callers do not have to wrap it.

## How the demo uses it

`PostProcess::deferredMat` binds the three handles by name:

```cpp
deferredMat.addTexture("uIrradianceMap",  ibl.irradianceHandle);
deferredMat.addTexture("uPrefilteredMap", ibl.prefilteredHandle);
deferredMat.addTexture("uBRDFLUT",        ibl.brdfLUTHandle);
```

`shaders/sky.frag` samples the equirectangular HDR directly via `ibl.equirectHandle` so the visible background matches the IBL light it provides.
