# Shadow Maps

This document explains how directional shadow mapping is implemented in Sonnet v2, covering the mathematical foundation, the three-pass GPU pipeline, and the engine-level plumbing that makes it work.

---

## 1. The Core Idea

A shadow map answers one question: **is a given surface point visible from the light source?**

The answer is found by comparing two depths measured along the direction from the light:

- The **closest depth** stored in the shadow map — the nearest surface the light can reach.
- The **current fragment's depth** from the light's perspective — how far this fragment is from the light.

If the current fragment is farther from the light than the closest recorded depth, something else is in between: the fragment is in shadow.

```
Light
 │
 │← closest depth (stored in shadow map) ── Occluder
 │
 │← fragment depth ────────────────────── Fragment (in shadow)
```

The shadow map is a 2D depth texture rendered from the light's point of view. It is sampled during the main scene pass to decide how much light reaches each fragment.

---

## 2. Coordinate Spaces

Three coordinate spaces are involved:

| Space | Description |
|---|---|
| **World space** | Shared space for all objects and the light. |
| **Light space** | The light's own view+projection, used during the shadow pass. |
| **NDC (light)** | Normalised device coordinates after the light projection. Depth is in `[0, 1]`. |

The transformation from world space to light-space NDC is encoded in a single matrix called the **light-space matrix**:

```
lightSpaceMat = lightProj × lightView
```

Every vertex's world-space position is multiplied by this matrix in the main vertex shader to produce `vLightSpacePos`, which is then used in the fragment shader to look up the shadow map.

---

## 3. Light-Space Matrix Construction

The directional light is treated as infinitely far away. Its position is simulated by placing a virtual camera far along the light direction, pointed at the scene origin:

```cpp
// apps/demo/main.cpp
const glm::vec3 lightDirNorm = glm::normalize(lightDir);
const glm::vec3 lightUp = std::abs(lightDirNorm.y) > 0.99f
                        ? glm::vec3{0.0f, 0.0f, 1.0f}   // avoid gimbal lock
                        : glm::vec3{0.0f, 1.0f, 0.0f};

const glm::mat4 lightView = glm::lookAt(
    lightDirNorm * 10.0f,   // camera position (10 units along light dir)
    glm::vec3{0.0f},        // look at scene origin
    lightUp
);

const glm::mat4 lightProj = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 1.0f, 20.0f);
const glm::mat4 lightSpaceMat = lightProj * lightView;
```

**Why orthographic?** A directional light has parallel rays — there is no perspective foreshortening. `glm::ortho` produces a parallel projection that maps the shadow frustum (an axis-aligned box in light space) into NDC. The bounds `±4.0` were chosen to cover the 6×6 floor and the rotating cube at the scene origin; `near=1.0` and `far=20.0` bound the depth range.

The `lightUp` guard handles the degenerate case where the light direction is nearly straight up or down: in that case the world Y axis cannot serve as the up vector for `glm::lookAt`, so the Z axis is used instead.

---

## 4. The Three-Pass Pipeline

Each frame executes three sequential render passes.

```
┌─────────────────────────────────────────────────────┐
│ Pass 1 – Shadow                                     │
│   RT:  shadowRT (2048×2048, depth only)             │
│   View: light-space orthographic                    │
│   Output: depth texture (shadow map)                │
└─────────────────────────────────────────────────────┘
                        │ shadow map
                        ▼
┌─────────────────────────────────────────────────────┐
│ Pass 2 – HDR Scene                                  │
│   RT:  hdrRT (framebuffer size, RGBA16F + depth)    │
│   View: camera perspective                          │
│   Output: HDR colour texture                        │
└─────────────────────────────────────────────────────┘
                        │ HDR colour texture
                        ▼
┌─────────────────────────────────────────────────────┐
│ Pass 3 – Tone-map                                   │
│   RT:  default framebuffer                          │
│   Draw: fullscreen quad                             │
│   Output: LDR colour → display                      │
└─────────────────────────────────────────────────────┘
```

### Pass 1 — Shadow Map

The shadow render target is a depth-only framebuffer (no colour attachments):

```cpp
const auto shadowRTHandle = renderer.createRenderTarget(RenderTargetDesc{
    .width  = SHADOW_SIZE,   // 2048
    .height = SHADOW_SIZE,
    .colors = {},            // no colour
    .depth  = TextureAttachmentDesc{
        .format      = TextureFormat::Depth24,
        .samplerDesc = {
            .minFilter = MinFilter::Nearest,
            .magFilter = MagFilter::Nearest,
            .wrapS     = TextureWrap::ClampToEdge,
            .wrapT     = TextureWrap::ClampToEdge,
        },
    },
});
```

`Nearest` filtering is deliberate — the PCF kernel (described in Section 6) manually samples neighbouring texels and averages, so hardware bilinear filtering on the depth texture would produce incorrect comparisons. `ClampToEdge` ensures that sampling beyond the shadow frustum boundary returns the edge depth value rather than wrapping, which would create false shadow artefacts.

When `colors` is empty, `GlRenderTarget::attachColorTextures` calls `glDrawBuffer(GL_NONE)` and `glReadBuffer(GL_NONE)` to tell OpenGL that this framebuffer intentionally has no colour output.

The shadow vertex shader only needs to transform geometry into light space:

```glsl
// SHADOW_VERT
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
```

The fragment shader is empty — OpenGL writes the interpolated depth to the depth buffer automatically.

The same scene geometry (cube + floor) is submitted through a shadow-pass queue, but with the shadow material bound instead of the lit material.

### Pass 2 — HDR Scene

The main scene is rendered into a 16-bit floating-point colour target. The `FrameContext` carries both the camera matrices and the light-space matrix so the renderer can upload `uLightSpaceMatrix` to the scene shader:

```cpp
// modules/renderer/frontend/src/Renderer.cpp  —  bindMaterial()
if (ctx.lightSpaceMatrix) {
    upload("uLightSpaceMatrix", *ctx.lightSpaceMatrix);
}
```

The shadow depth texture is bound to `uShadowMap` in the material alongside `uAlbedo`. The vertex shader computes `vLightSpacePos` per-vertex; the fragment shader samples the shadow map to decide the shadow factor.

### Pass 3 — Tone-map

A fullscreen quad converts HDR radiance values to display-range colours using the ACES filmic curve. Shadow mapping has no role in this pass.

---

## 5. Engine Plumbing

### Depth Texture as a Sampled Resource

The shadow depth texture lives inside the render target. To bind it as a `uShadowMap` sampler in the scene shader, it needs to be exposed as a `GPUTextureHandle`. `Renderer::depthTextureHandle` creates a **non-owning** wrapper:

```cpp
// modules/renderer/frontend/src/Renderer.cpp
GPUTextureHandle Renderer::depthTextureHandle(RenderTargetHandle handle) {
    const ITexture *tex = it->second->depthTexture();
    GPUTextureHandle texHandle{m_nextId++};
    m_textures.emplace(texHandle, std::make_unique<BorrowedTexture>(tex));
    return texHandle;
}
```

`BorrowedTexture` (defined at the top of `Renderer.cpp`) implements `ITexture` by forwarding all calls to the real texture owned by the render target. This allows the depth attachment to appear in the `m_textures` map and be bound like any other texture, without transferring ownership. Destroying the render target while the borrowed handle is still in use would dangle the pointer — the caller is responsible for ensuring the render target outlives any handles derived from it.

### FrameContext and lightSpaceMatrix

`FrameContext` is the per-frame data block passed to `Renderer::render`. The light-space matrix is carried as `std::optional<glm::mat4>` so it is absent in frames that do not use shadow mapping (such as the tone-map pass):

```cpp
// modules/api/include/sonnet/api/render/FrameContext.h
struct FrameContext {
    const glm::mat4 &viewMatrix;
    const glm::mat4 &projectionMatrix;
    // ...
    std::optional<glm::mat4> lightSpaceMatrix;
};
```

`Renderer::bindMaterial` only uploads `uLightSpaceMatrix` when the optional is populated, so shaders that do not declare this uniform are unaffected.

---

## 6. PCF Shadow Filtering

Raw shadow mapping produces hard, aliased edges because each fragment either fully passes or fully fails the depth comparison. **Percentage Closer Filtering (PCF)** softens the edges by sampling a neighbourhood of texels and averaging the per-sample pass/fail results.

The fragment shader samples a 3×3 kernel centred on the projected shadow-map coordinate:

```glsl
float shadowFactor(vec3 n) {
    // Transform from clip space to [0, 1] UV range.
    vec3 proj = vLightSpacePos.xyz / vLightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    // Fragments outside the shadow frustum are fully lit.
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
                        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float bias = max(uShadowBias * (1.0 - dot(n, normalize(uDirLight.direction))),
                     uShadowBias * 0.1);

    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(uShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += proj.z - bias > closest ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}
```

Each of the 9 samples votes 0.0 (in shadow) or 1.0 (lit). The average is a value in `[0, 1]` that acts as a smooth lighting multiplier. Fragments near a shadow boundary receive values between 0 and 1, producing soft penumbra edges.

The return value feeds the diffuse term:

```glsl
float shadow = shadowFactor(n);
vec3 col = (0.15 + diff * uDirLight.intensity * shadow) * uDirLight.color * albedo;
```

The `0.15` constant is ambient light — fragments in full shadow still receive a small base illumination rather than going completely black.

---

## 7. Shadow Bias

Depth buffer precision is finite. Without a bias, a surface comparing its own depth against the shadow map often sees its stored depth as very slightly closer, causing the surface to shadow itself — a pattern of dark stripes called **shadow acne**.

A constant bias offset alone is insufficient: the error grows on surfaces that are nearly parallel to the light direction (glancing angles). The implemented bias scales with the angle between the surface normal and the light direction:

```glsl
float bias = max(uShadowBias * (1.0 - dot(n, normalize(uDirLight.direction))),
                 uShadowBias * 0.1);
```

- When the surface faces the light directly (`dot = 1.0`), the bias is at its minimum: `uShadowBias × 0.1`.
- As the surface grazes the light (`dot → 0`), the bias approaches `uShadowBias`.
- The surface normal `n` is the interpolated, normalised fragment normal in world space.

`uShadowBias` is adjustable at runtime via the ImGui debug panel (range `0.0001` – `0.05`, default `0.005`). Increasing it eliminates acne but pushes shadows away from their casters (peter-panning). The slider allows tuning this trade-off while observing the result live.

---

## 8. Potential Improvements

| Limitation | Description | Possible fix |
|---|---|---|
| Fixed frustum | The `±4.0` ortho bounds are hardcoded to the demo scene. Lights far from the origin or scenes of different scale would lose coverage. | Fit the frustum to the camera's view frustum (Cascaded Shadow Maps) or to the scene's AABB. |
| Single cascade | Only one shadow map is produced. Objects far from the camera receive the same texel density as nearby objects. | Cascaded Shadow Maps (CSM) divide the view frustum into depth slices, each with its own shadow map. |
| 3×3 PCF kernel | The 9-sample kernel produces only a modest softening of shadow edges. | A larger Poisson-disk kernel or hardware `sampler2DShadow` with `GL_COMPARE_R_TO_TEXTURE` would improve quality. |
| Fixed resolution | The shadow map is always 2048×2048. | Expose resolution as a quality setting. |
| Point lights | Only the directional light casts shadows. | Point-light shadows require cube-map depth targets (six faces per light). |
