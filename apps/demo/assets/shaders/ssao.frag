#version 460 core
in  vec2 vUV;
out vec4 fragColor; // R = AO factor [0,1]

layout(SET(1,0)) uniform sampler2D uNormalMap; // view-space normals from pre-pass
layout(SET(1,1)) uniform sampler2D uDepthMap;  // depth texture from pre-pass
layout(SET(1,2)) uniform sampler2D uNoiseMap;  // 4x4 random rotation texture (tiled)

layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

// Kernel is ~1 KB and the per-draw scalars are small, so everything lives in
// the set=2 PerDraw UBO on Vulkan (plain uniforms on OpenGL).
#ifdef VULKAN
layout(std140, SET(2,0)) uniform PerDraw {
    vec3  uKernel[64];      // hemisphere sample offsets in tangent space
    vec2  uNoiseScale;      // vec2(viewportW/4, viewportH/4) for noise tiling
    float uRadius;          // SSAO sampling radius in view space
    float uBias;            // depth bias to avoid self-occlusion
} pd;
#define uKernel     pd.uKernel
#define uNoiseScale pd.uNoiseScale
#define uRadius     pd.uRadius
#define uBias       pd.uBias
#else
uniform vec3  uKernel[64];
uniform vec2  uNoiseScale;
uniform float uRadius;
uniform float uBias;
#endif

// Reconstruct view-space position from a depth texture sample.
vec3 viewPosFromDepth(vec2 uv) {
    float depth  = texture(uDepthMap, uv).r;       // [0,1]
    float ndcZ   = depth * 2.0 - 1.0;
    vec4  clipPos = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    vec4  viewPos = uInvProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    // View-space position and normal of this fragment.
    vec3 fragPos = viewPosFromDepth(vUV);
    // G-buffer stores world-space normals; transform to view space for SSAO.
    vec3 worldNormal = normalize(texture(uNormalMap, vUV).xyz);
    vec3 normal      = normalize(mat3(uView) * worldNormal);

    // Build a random TBN matrix using the noise texture (tiled).
    // The noise texture stores raw float values in [-1,1,0] — sample directly,
    // no [0,1]->[−1,1] remapping needed (that would corrupt the stored values).
    vec3 randomVec = normalize(texture(uNoiseMap, vUV * uNoiseScale).xyz);
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    // Accumulate occlusion over 64 hemisphere samples.
    float occlusion = 0.0;
    for (int i = 0; i < 64; ++i) {
        // Transform kernel sample from tangent space to view space.
        vec3 samplePos = TBN * uKernel[i];
        samplePos = fragPos + samplePos * uRadius;

        // Project sample onto screen to look up its depth.
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz  = offset.xyz * 0.5 + 0.5; // [-1,1] → [0,1]

        // Compare sample depth with geometry depth at that screen position.
        vec3 sampleGeomPos = viewPosFromDepth(offset.xy);
        float sampleDepth  = sampleGeomPos.z;

        // Range check: far-away fragments don't contribute.
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion  = 1.0 - (occlusion / 64.0);
    fragColor  = vec4(occlusion, 0.0, 0.0, 1.0);
}
