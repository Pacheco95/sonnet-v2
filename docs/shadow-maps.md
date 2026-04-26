# Shadow maps (demo)

The demo's shadow system, implemented in `apps/demo/ShadowMaps.{h,cpp}`, produces:

- **Cascaded shadow maps (CSM)** for the single directional light — `NUM_CASCADES = 3`, each `2048×2048` Depth24 with hardware shadow comparison enabled.
- **Omnidirectional point-light shadows** for up to `MAX_SHADOW_LIGHTS = 4` point lights — one `512×512` R32F cubemap per light, recording linear distance from the light.

The deferred lighting pass (`shaders/deferred_lighting.frag`) consumes both sets when shading.

## Why a shadow map at all?

A shadow map answers one question: **is a given surface point visible from the light?** It is a depth (or distance) texture rendered from the light's point of view; during shading, each fragment's projected depth is compared against the closest occluder recorded in the shadow map. If the fragment is farther, something is in between — it is in shadow.

## Directional shadows: cascaded shadow maps

A single directional shadow map sized to cover the entire view frustum wastes resolution far away from the camera and lacks resolution near it. CSM splits the camera frustum into depth slices ("cascades") and renders one shadow map per slice. Near slices use small frustums and produce dense texels; far slices use large frustums and produce sparse texels.

Each frame, `ShadowMaps::render()`:

1. Picks split distances along the camera's near→far range, blending logarithmic and uniform schemes (the classic PSSM mix). Splits land in `m_csmSplitDepths`.
2. For each cascade, computes the eight world-space corners of the camera sub-frustum bounded by `[splitNear, splitFar]`, transforms them into the directional-light's view space, fits a tight axis-aligned box, and stabilises it (snap to texel grid + extend along light-Z) to reduce shimmer.
3. Builds `lightProj × lightView` per cascade and stores it in `m_csmLightSpaceMats`.
4. Renders each enabled `RenderComponent` into the cascade's depth-only RT using a minimal "shadow" shader (vertex transforms position into light clip space; fragment is empty — depth is written automatically).

The cascade depth RTs are created with `SamplerDesc::depthCompare = true`, which sets `GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE` and `GL_TEXTURE_COMPARE_FUNC = GL_LEQUAL`. Sampling these textures via `sampler2DShadow` returns a hardware-filtered `[0, 1]` shadow factor rather than a raw depth — bilinear filtering then gives 4× free PCF samples per `texture()` call.

Inside the deferred lighting shader the cascade is selected by view-space depth:

```glsl
int cascade = 0;
for (int c = 0; c < NUM_CASCADES; ++c)
    if (viewZ < uCsmSplitDepths[c]) { cascade = c; break; }
vec4 lightSpace = uCsmLightSpaceMats[cascade] * vec4(worldPos, 1.0);
```

A small per-cascade PCF kernel (3×3 around the projected coord) builds on the hardware sampling and produces the directional shadow factor.

### Bias

A constant bias is too coarse: surfaces nearly parallel to the light direction self-shadow at glancing angles. The shader scales bias by the angle between surface normal and light direction, with a floor:

```glsl
float bias = max(uShadowBias * (1.0 - dot(N, L)), uShadowBias * 0.1);
```

`uShadowBias` is exposed in the editor as a slider (default `0.005`).

## Point-light shadows: cubemaps

For each point light that casts shadows (up to `MAX_SHADOW_LIGHTS`), `ShadowMaps` keeps a `512×512` R32F cubemap. The fragment shader writes the linear distance from the light to the fragment instead of a clip-space depth, which lets the lighting pass compare distances directly.

The implementation currently uses a raw OpenGL framebuffer with a renderbuffer for depth (`m_pointShadowFBO` / `m_pointShadowRBO`) and a geometry-shader path that emits each triangle to all six faces in one draw call (`shaders/point_shadow.{vert,frag}`). Each cubemap is registered with the engine `Renderer` as a `GPUTextureHandle` via `registerRawTexture`, so the deferred shader can sample it as a normal `samplerCube`.

`POINT_SHADOW_FAR = 25.0f` bounds the per-light shadow range. Fragments beyond that distance are considered fully lit (skipping the cubemap sample).

`pointShadowBias` is exposed alongside the directional bias in the editor.

## Plumbing

`ShadowMaps::render()` returns the number of point lights that produced shadow data this frame. The deferred lighting material reads:

- `uCsmLightSpaceMats[NUM_CASCADES]` — per-cascade matrix.
- `uCsmSplitDepths[NUM_CASCADES]` — per-cascade view-space far depth.
- `uShadowMaps[NUM_CASCADES]` — sampler2DShadow array bound from `csmDepthHandles()`.
- `uPointShadowMaps[MAX_SHADOW_LIGHTS]` — samplerCube array bound from `pointShadowHandles()`.
- `uShadowBias`, `uPointShadowBias` — tunables.

Shadow rendering happens before the G-buffer pass each frame, since the deferred lighting pass downstream needs all shadow textures populated. The graph in [Render graph](render-graph.md) does **not** schedule shadow passes — `ShadowMaps::render()` is called directly by the demo's main loop.

## Known limitations

- Point-shadow path still uses raw GL (geometry-shader fan-out + manual FBO). Phase 7 of the [Vulkan backend](vulkan-backend.md) work will add cubemap-layered rendering to the engine abstraction so this file can drop its glad dependency entirely.
- `ShadowMaps` exposes 1 directional + 4 point-light shadow casters. Adding more would require resizing the texture arrays and the deferred shader's uniform bindings.
- Cascade count is fixed at 3 at compile time.
