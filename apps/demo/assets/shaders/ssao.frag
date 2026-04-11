#version 330 core
in  vec2 vUV;
out vec4 fragColor; // R = AO factor [0,1]

uniform sampler2D uNormalMap; // view-space normals from pre-pass
uniform sampler2D uDepthMap;  // depth texture from pre-pass
uniform sampler2D uNoiseMap;  // 4x4 random rotation texture (tiled)

uniform vec3  uKernel[64];    // hemisphere sample offsets in tangent space
uniform mat4  uProjection;    // camera projection (for screen-space projection)
uniform mat4  uInvProjection; // inverse projection (for position reconstruction)
uniform vec2  uNoiseScale;    // vec2(viewportW/4, viewportH/4) for noise tiling
uniform float uRadius;        // SSAO sampling radius in view space
uniform float uBias;          // depth bias to avoid self-occlusion

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
    vec3 normal  = normalize(texture(uNormalMap, vUV).xyz);

    // Build a random TBN matrix using the noise texture (tiled).
    vec3 randomVec = normalize(texture(uNoiseMap, vUV * uNoiseScale).xyz * 2.0 - 1.0);
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
