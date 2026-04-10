#version 330 core
in  vec3 vNormal;
in  vec3 vFragPos;
in  vec2 vTexCoord;
in  vec4 vLightSpacePos;
out vec4 fragColor;

const float PI = 3.14159265359;

struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};
uniform DirLight        uDirLight;
uniform vec3            uViewPosition;
uniform sampler2D       uAlbedo;
uniform sampler2DShadow uShadowMap;
uniform float           uShadowBias;
uniform float           uMetallic;
uniform float           uRoughness;

// ── GGX Cook-Torrance BRDF ────────────────────────────────────────────────────

// Normal Distribution Function (Trowbridge-Reitz GGX)
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Schlick-GGX single term (used in Smith)
float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Smith geometry function — accounts for self-shadowing in both view and light dirs
float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Schlick Fresnel approximation
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── 3x3 PCF shadow factor (hardware sampler2DShadow) ─────────────────────────
float shadowFactor(vec3 n) {
    vec3 proj = vLightSpacePos.xyz / vLightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    // Outside shadow frustum -> fully lit
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
    // albedo is already in linear space — the texture was uploaded as GL_SRGB8
    // so the hardware linearises it on sample
    vec3 albedo = texture(uAlbedo, vTexCoord).rgb;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vFragPos);
    vec3 L = normalize(uDirLight.direction);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Base reflectivity: 0.04 for dielectrics, albedo for metals
    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);

    // Cook-Torrance specular BRDF
    float D = D_GGX(NdotH, uRoughness);
    float G = G_Smith(NdotV, NdotL, uRoughness);
    vec3  F = F_Schlick(HdotV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // Lambertian diffuse — metals absorb all diffuse
    vec3 kd      = (vec3(1.0) - F) * (1.0 - uMetallic);
    vec3 diffuse = kd * albedo / PI;

    // Directional light radiance
    vec3  radiance = uDirLight.color * uDirLight.intensity;
    float shadow   = shadowFactor(N);

    vec3 Lo = (diffuse + specular) * radiance * NdotL * shadow;

    // Constant ambient (no IBL yet)
    vec3 ambient = vec3(0.03) * albedo;

    // Output in linear HDR — tone-mapping is applied in the next pass
    fragColor = vec4(ambient + Lo, 1.0);
}
