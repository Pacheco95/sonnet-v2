#version 460 core
layout(location = 0) in vec3 vFragPos;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in mat3 vTBN; // 3 consecutive locations (2, 3, 4).

layout(location = 0) out vec4 gAlbedoRoughness; // albedo.rgb + roughness
layout(location = 1) out vec4 gNormalMetallic;  // world-space normal.rgb + metallic
layout(location = 2) out vec4 gEmissiveAO;      // emissive.rgb + ORM ao

layout(SET(1,0)) uniform sampler2D uAlbedo;
layout(SET(1,1)) uniform sampler2D uNormalMap;
layout(SET(1,2)) uniform sampler2D uORM;
layout(SET(1,3)) uniform sampler2D uEmissive;

// Mirrors gbuffer.vert's push layout identically — SPIRV-Reflect unions the
// stage flags for the single range, and the engine's Renderer can address
// each member by name through a single uniform map.
#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4  uModel;
    vec3  uEmissiveFactor;
    float uMetallic;
    float uRoughness;
    vec4  uAlbedoFactor;
    float uAlphaCutoff;
} pc;
#define uEmissiveFactor pc.uEmissiveFactor
#define uMetallic       pc.uMetallic
#define uRoughness      pc.uRoughness
#define uAlbedoFactor   pc.uAlbedoFactor
#define uAlphaCutoff    pc.uAlphaCutoff
#else
uniform vec3  uEmissiveFactor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec4  uAlbedoFactor;  // per-material tint/scale (default {1,1,1,1})
uniform float uAlphaCutoff;   // 0.0 = disabled; > 0.0 = alpha-mask cutoff
#endif

void main() {
    vec4 albedoSample = texture(uAlbedo, vTexCoord);
    albedoSample.rgb *= uAlbedoFactor.rgb;
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) discard;

    // Normal from tangent-space map → world space via TBN.
    vec3 tangentNormal = texture(uNormalMap, vTexCoord).rgb * 2.0 - 1.0;
    vec3 N = normalize(vTBN * tangentNormal);

    // ORM: R=occlusion, G=roughness, B=metallic (glTF convention).
    vec3  orm       = texture(uORM, vTexCoord).rgb;
    float ao        = orm.r;
    float roughness = clamp(orm.g * uRoughness, 0.04, 1.0);
    float metallic  = clamp(orm.b * uMetallic,  0.0,  1.0);

    vec3 emissive = texture(uEmissive, vTexCoord).rgb * uEmissiveFactor;

    gAlbedoRoughness = vec4(albedoSample.rgb, roughness);
    gNormalMetallic  = vec4(N, metallic);
    gEmissiveAO      = vec4(emissive, ao);
}
