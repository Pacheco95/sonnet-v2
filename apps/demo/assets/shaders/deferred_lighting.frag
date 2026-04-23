#version 460 core
in  vec2 vUV;
out vec4 fragColor;

// ── G-buffer inputs + shadow maps + IBL — all in set=1 ────────────────────────
// Array bindings (uShadowMaps[3], uPointShadowMaps[4]) will compile under both
// backends; the Vulkan backend's set=1 descriptor allocator needs multi-binding
// support (Phase 7b) before this pass actually runs under Vulkan.
layout(SET(1,0))  uniform sampler2D       gAlbedoRoughness; // .rgb = albedo, .a = roughness
layout(SET(1,1))  uniform sampler2D       gNormalMetallic;  // .rgb = world-space normal, .a = metallic
layout(SET(1,2))  uniform sampler2D       gEmissiveAO;      // .rgb = emissive, .a = ORM ambient-occlusion
layout(SET(1,3))  uniform sampler2D       gDepth;           // hardware depth [0,1]

// ── Cascaded shadow maps (directional light) ──────────────────────────────────
#define NUM_CASCADES 3
layout(SET(1,4))  uniform sampler2DShadow uShadowMaps[NUM_CASCADES];

// ── Point-light shadow cubemaps ───────────────────────────────────────────────
#define MAX_SHADOW_LIGHTS 4
layout(SET(1,7))  uniform samplerCube     uPointShadowMaps[MAX_SHADOW_LIGHTS];

// ── IBL ───────────────────────────────────────────────────────────────────────
layout(SET(1,11)) uniform samplerCube     uIrradianceMap;
layout(SET(1,12)) uniform samplerCube     uPrefilteredMap;
layout(SET(1,13)) uniform sampler2D       uBRDFLUT;
layout(SET(1,14)) uniform sampler2D       uSSAO;      // blurred screen-space AO

// ── Camera UBO (set=0, binding=0) ─────────────────────────────────────────────
layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

// ── Lights UBO (set=0, binding=1) — frame-wide per plan §7 ────────────────────
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

layout(std140, SET(0,1)) uniform LightsUBO {
    DirLight   uDirLight;
    PointLight uPointLights[MAX_POINT_LIGHTS];
    int        uPointLightCount;
};

// ── Per-draw UBO for cascade matrices + split depths (too big for push) ───────
#ifdef VULKAN
layout(std140, SET(2,0)) uniform PerDraw {
    mat4  uCSMLightSpaceMats[NUM_CASCADES];
    float uCSMSplitDepths[NUM_CASCADES]; // view-space far depths (positive)
} pd;
#define uCSMLightSpaceMats pd.uCSMLightSpaceMats
#define uCSMSplitDepths    pd.uCSMSplitDepths
#else
uniform mat4  uCSMLightSpaceMats[NUM_CASCADES];
uniform float uCSMSplitDepths[NUM_CASCADES];
#endif

// ── Small per-frame scalars in push constants ─────────────────────────────────
#ifdef VULKAN
layout(push_constant) uniform Push {
    float uShadowBias;
    float uPointShadowFarPlane;
    float uPointShadowBias;
    int   uPointShadowCount;
    float uMaxPrefilteredLOD;
} pc;
#define uShadowBias          pc.uShadowBias
#define uPointShadowFarPlane pc.uPointShadowFarPlane
#define uPointShadowBias     pc.uPointShadowBias
#define uPointShadowCount    pc.uPointShadowCount
#define uMaxPrefilteredLOD   pc.uMaxPrefilteredLOD
#else
uniform float uShadowBias;
uniform float uPointShadowFarPlane;
uniform float uPointShadowBias;
uniform int   uPointShadowCount;
uniform float uMaxPrefilteredLOD;
#endif

const float PI = 3.14159265359;

// 20 fixed offsets for point-shadow PCF (axis-aligned + diagonal directions).
const vec3 shadowOffsets[20] = vec3[](
    vec3( 1, 1, 1), vec3( 1,-1, 1), vec3(-1,-1, 1), vec3(-1, 1, 1),
    vec3( 1, 1,-1), vec3( 1,-1,-1), vec3(-1,-1,-1), vec3(-1, 1,-1),
    vec3( 1, 1, 0), vec3( 1,-1, 0), vec3(-1,-1, 0), vec3(-1, 1, 0),
    vec3( 1, 0, 1), vec3(-1, 0, 1), vec3( 1, 0,-1), vec3(-1, 0,-1),
    vec3( 0, 1, 1), vec3( 0,-1, 1), vec3( 0,-1,-1), vec3( 0, 1,-1)
);

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

// ── Cascaded PCF shadow (3×3 kernel, hardware sampler2DShadow) ───────────────
float shadowFactor(vec3 worldPos, vec3 N) {
    // View-space depth selects which cascade to sample.
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    int cascade = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES - 1; ++i) {
        if (viewDepth < uCSMSplitDepths[i]) {
            cascade = i;
            break;
        }
    }

    vec4 lightSpacePos = uCSMLightSpaceMats[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
                         proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = max(uShadowBias * (1.0 - dot(N, normalize(uDirLight.direction))),
                     uShadowBias * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMaps[cascade], 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            shadow += texture(uShadowMaps[cascade],
                              vec3(proj.xy + vec2(x, y) * texelSize, proj.z - bias));
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

        // ── Point-light shadow (PCF over 20 cubemap samples) ─────────────────
        float ptShadow = 1.0;
        if (i < uPointShadowCount) {
            vec3  fragToLight   = worldPos - uPointLights[i].position;
            float currentDist   = length(fragToLight) / uPointShadowFarPlane;
            float diskRadius    = (1.0 + currentDist) / 25.0;
            float occluded      = 0.0;
            for (int j = 0; j < 20; ++j) {
                float closestDist = texture(uPointShadowMaps[i],
                                           fragToLight + shadowOffsets[j] * diskRadius).r;
                if (currentDist - uPointShadowBias > closestDist)
                    occluded += 1.0;
            }
            ptShadow = 1.0 - occluded / 20.0;
        }

        Lo += (diff_p + spec_p) * rad_p * NdotLp * ptShadow;
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
