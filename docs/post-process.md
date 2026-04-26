# Post-processing pipeline (demo)

The demo's renderer is a deferred shading pipeline with a stack of post-process passes wired together by the [render graph](render-graph.md). All passes are implemented in `apps/demo/PostProcess.{h,cpp}` against shaders in `apps/demo/assets/shaders/`.

## Frame layout

```
shadow maps           (CSM cascades + per-point-light cubemaps)
   │
   ▼
G-buffer pass         (mrt: AlbedoRoughness, NormalMetallic, EmissiveAO + depth)
   │
   ▼
SSAO  → SSAO blur     (optional; reads normal + depth; outputs single-channel AO)
   │
   ▼
Deferred lighting     (PBR + IBL + shadow sampling → HDR target)
   │
   ▼
Sky                   (reads HDR target, draws environment map where depth==far)
   │
   ▼
SSR                   (optional; reads HDR + g-buffer + depth)
   │
   ▼
Bloom bright → blur H → blur V (optional)
   │
   ▼
Tonemap               (HDR → LDR; ACES filmic; exposure)
   │
   ▼
FXAA                  (optional; LDR antialiasing)
   │
   ▼
Outline mask + outline (optional; selected object highlight)
   │
   ▼
Viewport texture      (consumed by ImGui::Image in the editor's viewport panel)
```

## Render targets

Defined in `apps/demo/RenderTargets.{h,cpp}`. Screen-sized targets:

| RT | Format | Purpose |
|---|---|---|
| `gbufRT` | RGBA16F × 3 + Depth24 | Albedo+roughness, normal+metallic, emissive+AO, depth |
| `hdrRT`  | RGBA16F + Depth24 | Deferred lighting output |
| `ssaoRT` / `ssaoBlurRT` | R16F | SSAO occlusion + blur |
| `bloomBrightRT` / `bloomBlurRT` | RGBA16F | Bloom intermediate buffers |
| `ssrRT` | RGBA16F | Screen-space reflections |
| `outlineMaskRT` | R8 | Selected-object stencil |
| `pickingRT` | RGBA8 | Object IDs for mouse picking |
| `ldrRT` / `viewportRT` | RGBA8 sRGB | Final output (post-tonemap, post-FXAA) |

Texture handles obtained via `Renderer::colorTextureHandle(rt, i)` survive `resizeRenderTarget` — `RenderTargets::resize()` recreates the underlying RTs at the new size while material instances keep referring to the same handles.

## G-buffer

`shaders/gbuffer.vert` + `gbuffer.frag` write three MRTs:

- RT0: `vec4(albedo.rgb, perceptualRoughness)`
- RT1: `vec4(N.xyz, metallic)`
- RT2: `vec4(emissive.rgb, ao)`

Skinned meshes use `gbuffer_skinned.vert`, which reads `BoneIndex(6)` and `BoneWeight(7)` attributes and the `uBoneMatrices[]` palette uploaded per frame from `SkinComponent`. Emissive-only objects use `gbuffer_emissive.frag`.

## Deferred lighting

`shaders/deferred_lighting.frag` is a fullscreen pass that:

1. Reconstructs world-space position from the g-buffer depth and the camera's inverse view-projection.
2. Samples the directional light's CSM cascades (chosen by view-space depth) and applies PCF.
3. For each shadow-casting point light, samples its cubemap shadow map.
4. Evaluates the PBR BRDF (Cook-Torrance GGX + Lambert), with IBL split-sum approximation reading `uIrradianceMap`, `uPrefilteredMap`, and `uBRDFLUT`.
5. Multiplies emissive and AO contributions in.

The result lands in the HDR RT.

## Sky pass

`shaders/sky.{vert,frag}` draws a fullscreen quad after deferred lighting, sampling the equirectangular HDR environment map and rejecting fragments whose g-buffer depth is closer than the far plane (`depth < 1.0`).

## SSAO

Hemisphere-sampled SSAO with 64 kernel samples generated at startup (cosine-weighted, biased toward origin) plus a 4×4 RGBA32F repeating noise texture for tangent-space rotations:

- `ssao.frag` — reads view-space normal (reconstructed from g-buffer normal + view matrix) and view-space depth, samples `uSamples[64]` with the noise rotation, returns `[0, 1]` occlusion.
- `ssao_blur.frag` — 4×4 box blur to clean up the noise pattern.
- `ssao_show.frag` — debug pass that bypasses lighting to visualise occlusion alone.

`ssaoRadius`, `ssaoBias`, `ssaoEnabled`, and `ssaoShow` are exposed in the editor.

## Bloom

Three-step Kawase-style separable bloom:

1. `bloom_bright.frag` — extracts texels above `uBloomThreshold` from the HDR target.
2. `bloom_blur.frag` — gaussian blur, applied N times alternating horizontal/vertical (`uBloomIterations` per direction).
3. The tonemap pass blends the blurred bloom in additively scaled by `uBloomIntensity`.

## SSR

`shaders/ssr.frag` does view-space ray-march reflections off the HDR + g-buffer for fragments whose roughness is below `uSsrRoughnessMax`. Tunables: `uSsrMaxSteps`, `uSsrStepSize`, `uSsrThickness`, `uSsrMaxDistance`, `uSsrStrength`. The result is composited into the HDR target before tonemapping.

## Tonemap

`shaders/tonemap.frag` applies an ACES filmic curve scaled by `uExposure`, blends bloom in additively, and outputs to `ldrRT` (sRGB-encoded).

## FXAA

`shaders/fxaa.frag` is a standard luma-edge FXAA 3.11 implementation reading `ldrRT` and writing back a viewport-quality LDR texture. Toggleable.

## Outline

When an object is selected in the editor:

1. `outline_mask.frag` (or `outline_mask_skinned.vert`) draws the selected object into a single-channel mask RT.
2. `outline.frag` samples the mask with a small kernel and emits the configured outline colour where the mask edges are.

## Picking

The editor renders the scene a second time into `pickingRT` using `picking.{vert,frag}`, encoding each object's ID as RGBA. On mouse click, `Renderer::readPixelRGBA8(pickingRT, 0, x, y)` reads back one pixel and decodes the ID. Skinned objects use `picking_skinned` variants of the materials.

## See also

- [Shadow maps](shadow-maps.md) — CSM + point-light shadows feeding deferred lighting.
- [IBL](ibl.md) — the irradiance/prefilter/BRDF-LUT bake that deferred lighting reads.
- [Render graph](render-graph.md) — how the passes are sequenced and culled.
