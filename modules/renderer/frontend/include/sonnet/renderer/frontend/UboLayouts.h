#pragma once

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/Light.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cstddef>

namespace sonnet::renderer::frontend {

// ── CameraUBO (binding = 0) ───────────────────────────────────────────────────
// std140 layout, 272 bytes.
// Uploaded once per Renderer::render() call from FrameContext.
struct CameraUBO {
    alignas(16) glm::mat4 uView;           // offset   0
    alignas(16) glm::mat4 uProjection;     // offset  64
    alignas(16) glm::vec3 uViewPosition;   // offset 128
    float                 _pad0 = 0.f;     // offset 140  (std140 gap after vec3)
    alignas(16) glm::mat4 uInvViewProj;    // offset 144
    alignas(16) glm::mat4 uInvProjection;  // offset 208
};
static_assert(sizeof(CameraUBO) == 272);
static_assert(offsetof(CameraUBO, uView)          ==   0);
static_assert(offsetof(CameraUBO, uProjection)    ==  64);
static_assert(offsetof(CameraUBO, uViewPosition)  == 128);
static_assert(offsetof(CameraUBO, uInvViewProj)   == 144);
static_assert(offsetof(CameraUBO, uInvProjection) == 208);

// ── LightsUBO helper structs (std140) ─────────────────────────────────────────

// Mirrors GLSL `struct DirLight { vec3 direction; vec3 color; float intensity; }`
// with explicit padding to match std140 memory layout (vec3 base alignment = 16).
struct DirLightStd140 {
    alignas(16) glm::vec3 direction; float _pad0 = 0.f;  // offset  0 (12 + 4 pad)
    glm::vec3             color;                          // offset 16 (12 bytes)
    float                 intensity;                      // offset 28
};                                                        //             32 bytes
static_assert(sizeof(DirLightStd140) == 32);

// Mirrors GLSL `struct PointLight { vec3 position; vec3 color; float intensity;
//   float constant; float linear; float quadratic; }` with std140 padding.
struct PointLightStd140 {
    alignas(16) glm::vec3 position; float _pad0 = 0.f;  // offset  0 (12 + 4 pad)
    glm::vec3             color;                         // offset 16 (12 bytes)
    float intensity;                                     // offset 28
    float constant;                                      // offset 32
    float linear;                                        // offset 36
    float quadratic;                                     // offset 40
    float _pad1 = 0.f;                                   // offset 44 (tail pad to 48)
};                                                       //             48 bytes
static_assert(sizeof(PointLightStd140) == 48);

// ── LightsUBO (binding = 1) ───────────────────────────────────────────────────
// std140 layout, 432 bytes.
static constexpr int kMaxPointLightsUBO = 8;

struct LightsUBO {
    DirLightStd140   uDirLight;                       // offset   0  (32 bytes)
    PointLightStd140 uPointLights[kMaxPointLightsUBO]; // offset  32  (8×48 = 384 bytes)
    int              uPointLightCount;                 // offset 416
    float            _pad[3] = {};                     // offset 420  (pad to 432)
};
static_assert(sizeof(LightsUBO) == 432);
static_assert(offsetof(LightsUBO, uDirLight)        ==   0);
static_assert(offsetof(LightsUBO, uPointLights)     ==  32);
static_assert(offsetof(LightsUBO, uPointLightCount) == 416);

// ── Helpers to populate UBOs from engine types ────────────────────────────────

inline CameraUBO buildCameraUBO(const api::render::FrameContext &ctx) {
    CameraUBO c{};
    c.uView          = ctx.viewMatrix;
    c.uProjection    = ctx.projectionMatrix;
    c.uViewPosition  = ctx.viewPosition;
    c.uInvViewProj   = glm::inverse(ctx.projectionMatrix * ctx.viewMatrix);
    c.uInvProjection = glm::inverse(ctx.projectionMatrix);
    return c;
}

inline LightsUBO buildLightsUBO(const api::render::FrameContext &ctx) {
    LightsUBO l{};
    if (ctx.directionalLight) {
        const auto &dl       = *ctx.directionalLight;
        l.uDirLight.direction = dl.direction;
        l.uDirLight.color     = dl.color;
        l.uDirLight.intensity = dl.intensity;
    }
    const int count = static_cast<int>(
        std::min(ctx.pointLights.size(), static_cast<std::size_t>(kMaxPointLightsUBO)));
    for (int i = 0; i < count; ++i) {
        const auto &pl             = ctx.pointLights[static_cast<std::size_t>(i)];
        l.uPointLights[i].position  = pl.position;
        l.uPointLights[i].color     = pl.color;
        l.uPointLights[i].intensity = pl.intensity;
        l.uPointLights[i].constant  = pl.constant;
        l.uPointLights[i].linear    = pl.linear;
        l.uPointLights[i].quadratic = pl.quadratic;
    }
    l.uPointLightCount = count;
    return l;
}

} // namespace sonnet::renderer::frontend
