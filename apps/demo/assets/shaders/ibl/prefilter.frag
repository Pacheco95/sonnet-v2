#version 460 core
in  vec3 vLocalPos;
out vec4 fragColor;

layout(SET(1,0)) uniform samplerCube uEnvMap;

#ifdef VULKAN
layout(push_constant) uniform Push {
    layout(offset = 0) float uRoughness;
} pc;
#define uRoughness pc.uRoughness
#else
uniform float uRoughness;
#endif

const float PI = 3.14159265359;

// ── Low-discrepancy sequence (Hammersley) ─────────────────────────────────────

float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverse_VdC(i));
}

// ── GGX importance sampling ───────────────────────────────────────────────────

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian in tangent space.
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // Tangent-space basis.
    vec3 up    = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T     = normalize(cross(up, N));
    vec3 B     = cross(N, T);

    return normalize(T * H.x + B * H.y + N * H.z);
}

// ── Main ──────────────────────────────────────────────────────────────────────

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 R = N;
    vec3 V = R; // assume view == reflection for pre-filtering

    const uint  SAMPLE_COUNT    = 1024u;
    float       totalWeight     = 0.0;
    vec3        prefilteredColor = vec3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(Xi, N, uRoughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += texture(uEnvMap, L).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }

    prefilteredColor /= max(totalWeight, 0.001);
    fragColor = vec4(prefilteredColor, 1.0);
}
