#version 330 core
in  vec2 vUV;
out vec4 fragColor;

// ── G-buffer inputs ───────────────────────────────────────────────────────────
uniform sampler2D gAlbedoRoughness; // .rgb = albedo, .a = roughness
uniform sampler2D gNormalMetallic;  // .rgb = world-space normal, .a = metallic
uniform sampler2D gEmissiveAO;      // .rgb = emissive, .a = ORM ambient-occlusion
uniform sampler2D gDepth;           // hardware depth [0,1]

// ── Lighting ──────────────────────────────────────────────────────────────────
uniform sampler2DShadow uShadowMap;
uniform float           uShadowBias;
uniform samplerCube     uIrradianceMap;
uniform samplerCube     uPrefilteredMap;
uniform sampler2D       uBRDFLUT;
uniform float           uMaxPrefilteredLOD;
uniform sampler2D       uSSAO;      // blurred screen-space AO

// ── Matrices and camera ───────────────────────────────────────────────────────
uniform mat4  uInvViewProj;      // inverse(proj * view) — reconstruct world pos from depth
uniform mat4  uLightSpaceMatrix; // for PCF shadow sampling
uniform vec3  uViewPosition;

// ── Lights ────────────────────────────────────────────────────────────────────
struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};
uniform DirLight uDirLight;

struct PointLight {
    vec3  position;
    vec3  color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
};
#define MAX_POINT_LIGHTS 8
uniform PointLight uPointLights[MAX_POINT_LIGHTS];
uniform int        uPointLightCount;

const float PI = 3.14159265359;

// ── GGX Cook-Torrance BRDF helpers ───────────────────────────────────────────
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

// ── Position reconstruction ───────────────────────────────────────────────────
vec3 worldPosFromDepth(vec2 uv) {
    float depth = texture(gDepth, uv).r;
    float ndcZ  = depth * 2.0 - 1.0;
    vec4  clip  = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    vec4  world = uInvViewProj * clip;
    return world.xyz / world.w;
}

// ── 3×3 PCF shadow (hardware sampler2DShadow) ─────────────────────────────────
float shadowFactor(vec3 worldPos, vec3 N) {
    vec4 lightSpacePos = uLightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
                         proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = max(uShadowBias * (1.0 - dot(N, normalize(uDirLight.direction))),
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
    // Unpack G-buffer.
    vec4  ar        = texture(gAlbedoRoughness, vUV);
    vec4  nm        = texture(gNormalMetallic,  vUV);
    vec4  ea        = texture(gEmissiveAO,      vUV);

    vec3  albedo    = ar.rgb;
    float roughness = ar.a;
    vec3  N         = normalize(nm.rgb);
    float metallic  = nm.a;
    vec3  emissive  = ea.rgb;
    float ao        = ea.a;

    vec3  worldPos  = worldPosFromDepth(vUV);
    float ssao      = texture(uSSAO, vUV).r;

    vec3 V    = normalize(uViewPosition - worldPos);
    vec3 L    = normalize(uDirLight.direction);
    vec3 H    = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ── Directional light ─────────────────────────────────────────────────────
    float D       = D_GGX(NdotH, roughness);
    float G       = G_Smith(NdotV, NdotL, roughness);
    vec3  F       = F_Schlick(HdotV, F0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kd       = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse  = kd * albedo / PI;
    vec3 radiance = uDirLight.color * uDirLight.intensity;
    float shadow  = shadowFactor(worldPos, N);
    vec3 Lo = (diffuse + specular) * radiance * NdotL * shadow;

    // ── Point lights ──────────────────────────────────────────────────────────
    for (int i = 0; i < uPointLightCount; ++i) {
        vec3  Lp     = normalize(uPointLights[i].position - worldPos);
        vec3  Hp     = normalize(V + Lp);
        float dist   = length(uPointLights[i].position - worldPos);
        float atten  = 1.0 / (uPointLights[i].constant
                            + uPointLights[i].linear    * dist
                            + uPointLights[i].quadratic * dist * dist);
        float NdotLp = max(dot(N, Lp), 0.0);
        float NdotHp = max(dot(N, Hp), 0.0);
        float HdotVp = max(dot(Hp, V), 0.0);
        float Dp     = D_GGX(NdotHp, roughness);
        float Gp     = G_Smith(NdotV, NdotLp, roughness);
        vec3  Fp     = F_Schlick(HdotVp, F0);
        vec3 spec_p  = (Dp * Gp * Fp) / max(4.0 * NdotV * NdotLp, 0.001);
        vec3 kd_p    = (vec3(1.0) - Fp) * (1.0 - metallic);
        vec3 diff_p  = kd_p * albedo / PI;
        vec3 rad_p   = uPointLights[i].color * uPointLights[i].intensity * atten;
        Lo += (diff_p + spec_p) * rad_p * NdotLp;
    }

    // ── IBL ambient (split-sum) ───────────────────────────────────────────────
    vec3 F_amb  = F_Schlick(NdotV, F0);
    vec3 kd_amb = (vec3(1.0) - F_amb) * (1.0 - metallic);

    vec3 irradiance  = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL  = kd_amb * irradiance * albedo;

    vec3 R                = reflect(-V, N);
    vec3 prefilteredColor = textureLod(uPrefilteredMap, R, roughness * uMaxPrefilteredLOD).rgb;
    vec2 brdf             = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL      = prefilteredColor * (F_amb * brdf.x + brdf.y);

    vec3 ambient = (diffuseIBL + specularIBL) * ao * ssao;

    fragColor = vec4(ambient + Lo + emissive, 1.0);
}
