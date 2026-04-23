#version 460 core
in  vec3 vFragPos;
in  vec2 vTexCoord;
in  vec4 vLightSpacePos;
in  mat3 vTBN;
out vec4 fragColor;

const float PI = 3.14159265359;

// ── Camera UBO (binding = 0) ──────────────────────────────────────────────────
layout(std140, binding = 0) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

// ── Lights UBO (binding = 1) ──────────────────────────────────────────────────
struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};

struct PointLight {
    vec3  position;
    vec3  color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
};
#define MAX_POINT_LIGHTS 8

layout(std140, binding = 1) uniform LightsUBO {
    DirLight   uDirLight;
    PointLight uPointLights[MAX_POINT_LIGHTS];
    int        uPointLightCount;
};
uniform sampler2D       uAlbedo;
// Normal map in tangent space (OpenGL convention: +Y = up).
// Bind a 1x1 {128,128,255} flat-normal texture when no map is available.
uniform sampler2D       uNormalMap;
// ORM map: R=occlusion, G=roughness, B=metallic (glTF convention).
// Bind a 1x1 white texture when no map is available — scalars drive values directly.
uniform sampler2D       uORM;
uniform sampler2DShadow uShadowMap;
uniform float           uShadowBias;
uniform float           uMetallic;          // multiplied on top of uORM.b
uniform float           uRoughness;         // multiplied on top of uORM.g
uniform samplerCube     uIrradianceMap;     // diffuse IBL
uniform samplerCube     uPrefilteredMap;    // specular IBL (mipped by roughness)
uniform sampler2D       uBRDFLUT;           // GGX split-sum LUT
uniform float           uMaxPrefilteredLOD; // mip count - 1 in uPrefilteredMap
uniform sampler2D       uSSAO;              // screen-space AO (blurred, R channel)
uniform sampler2D       uEmissive;          // emissive/glow map (sRGB)
uniform vec3            uEmissiveFactor;    // per-material emissive multiplier (default 0)
uniform float           uAlphaCutoff;       // > 0.0 enables alpha-mask discard

// ── GGX Cook-Torrance BRDF ────────────────────────────────────────────────────

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── 3x3 PCF shadow factor (hardware sampler2DShadow) ─────────────────────────
float shadowFactor(vec3 n) {
    vec3 proj = vLightSpacePos.xyz / vLightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
                        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = max(uShadowBias * (1.0 - dot(n, normalize(uDirLight.direction))),
                     uShadowBias * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            shadow += texture(uShadowMap, vec3(proj.xy + vec2(x, y) * texelSize, proj.z - bias));
    return shadow / 9.0;
}

// ── Main ──────────────────────────────────────────────────────────────────────
void main() {
    // Albedo — already linear (texture uploaded as GL_SRGB8, hardware linearises on sample)
    vec4 albedoSample = texture(uAlbedo, vTexCoord);
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) discard;
    vec3 albedo = albedoSample.rgb;

    // Normal from tangent-space map -> world space via TBN
    vec3 tangentNormal = texture(uNormalMap, vTexCoord).rgb * 2.0 - 1.0;
    vec3 N = normalize(vTBN * tangentNormal);

    // ORM: scalar uniforms act as multipliers (glTF metallicFactor / roughnessFactor)
    vec3  orm       = texture(uORM, vTexCoord).rgb;
    float ao        = orm.r;
    float roughness = clamp(orm.g * uRoughness, 0.04, 1.0);
    float metallic  = clamp(orm.b * uMetallic,  0.0,  1.0);

    vec3 V = normalize(uViewPosition - vFragPos);
    vec3 L = normalize(uDirLight.direction);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    vec3  F = F_Schlick(HdotV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    vec3 kd      = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / PI;

    vec3  radiance = uDirLight.color * uDirLight.intensity;
    float shadow   = shadowFactor(N);

    vec3 Lo = (diffuse + specular) * radiance * NdotL * shadow;

    // ── Point lights ──────────────────────────────────────────────────────────
    for (int i = 0; i < uPointLightCount; ++i) {
        vec3  Lp       = normalize(uPointLights[i].position - vFragPos);
        vec3  Hp       = normalize(V + Lp);
        float dist     = length(uPointLights[i].position - vFragPos);
        float atten    = 1.0 / (uPointLights[i].constant
                              + uPointLights[i].linear    * dist
                              + uPointLights[i].quadratic * dist * dist);
        float NdotLp   = max(dot(N, Lp), 0.0);
        float NdotHp   = max(dot(N, Hp), 0.0);
        float HdotVp   = max(dot(Hp, V), 0.0);
        float Dp       = D_GGX(NdotHp, roughness);
        float Gp       = G_Smith(NdotV, NdotLp, roughness);
        vec3  Fp       = F_Schlick(HdotVp, F0);
        vec3  spec_p   = (Dp * Gp * Fp) / max(4.0 * NdotV * NdotLp, 0.001);
        vec3  kd_p     = (vec3(1.0) - Fp) * (1.0 - metallic);
        vec3  diff_p   = kd_p * albedo / PI;
        vec3  rad_p    = uPointLights[i].color * uPointLights[i].intensity * atten;
        Lo += (diff_p + spec_p) * rad_p * NdotLp;
    }

    // ── IBL ambient (split-sum approximation) ─────────────────────────────────
    // Fresnel at grazing angle (use NdotV, not HdotV, for the ambient term).
    vec3 F_amb = F_Schlick(NdotV, F0);

    // Diffuse IBL: irradiance map × kd × albedo
    vec3 kd_amb     = (vec3(1.0) - F_amb) * (1.0 - metallic);
    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = kd_amb * irradiance * albedo;

    // Specular IBL: pre-filtered env × (F0 * scale + bias) from BRDF LUT
    vec3 R               = reflect(-V, N);
    vec3 prefilteredColor = textureLod(uPrefilteredMap, R,
                                       roughness * uMaxPrefilteredLOD).rgb;
    vec2 brdf            = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL     = prefilteredColor * (F_amb * brdf.x + brdf.y);

    // Combine ORM ambient-occlusion with screen-space AO.
    vec2  screenUV  = gl_FragCoord.xy / vec2(textureSize(uSSAO, 0));
    float ssao      = texture(uSSAO, screenUV).r;
    vec3 ambient = (diffuseIBL + specularIBL) * ao * ssao;

    vec3 emissive = texture(uEmissive, vTexCoord).rgb * uEmissiveFactor;
    fragColor = vec4(ambient + Lo + emissive, 1.0);
}
