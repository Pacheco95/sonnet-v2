#version 330 core
in vec3 vFragPos;
in vec2 vTexCoord;
in mat3 vTBN;

layout(location = 0) out vec4 gAlbedoRoughness; // albedo.rgb + roughness
layout(location = 1) out vec4 gNormalMetallic;  // world-space normal.rgb + metallic
layout(location = 2) out vec4 gEmissiveAO;      // emissive.rgb + ORM ao

uniform sampler2D uAlbedo;
uniform sampler2D uNormalMap;
uniform sampler2D uORM;
uniform sampler2D uEmissive;
uniform vec3      uEmissiveFactor;
uniform float     uMetallic;
uniform float     uRoughness;
uniform vec4      uAlbedoFactor;  // per-material tint/scale (default {1,1,1,1})
uniform float     uAlphaCutoff;  // 0.0 = disabled; > 0.0 = alpha-mask cutoff

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
