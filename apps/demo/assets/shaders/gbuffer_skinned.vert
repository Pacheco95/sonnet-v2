#version 460 core
// Vertex layout: position(0), texcoord(2), normal(3), tangent(4), bitangent(5),
//                boneIndices(6), boneWeights(7)
layout(location = 0) in vec3  aPosition;
layout(location = 2) in vec2  aTexCoord;
layout(location = 3) in vec3  aNormal;
layout(location = 4) in vec3  aTangent;
layout(location = 5) in vec3  aBiTangent;
layout(location = 6) in ivec4 aBoneIndices;
layout(location = 7) in vec4  aBoneWeights;

const int MAX_BONES = 128;

layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

// Bone matrices (128 × mat4 = 8 KB) don't fit in push constants; they live in
// the set=2 PerDraw UBO. Shared between gbuffer_skinned.vert and its matching
// fragment shader (plain gbuffer.frag) for push-constant material scalars.
#ifdef VULKAN
layout(std140, SET(2,0)) uniform PerDraw {
    mat4 uBoneMatrices[MAX_BONES];
} pd;
#define uBoneMatrices pd.uBoneMatrices
layout(push_constant) uniform Push {
    mat4  uModel;
    vec3  uEmissiveFactor;
    float uMetallic;
    float uRoughness;
    vec4  uAlbedoFactor;
    float uAlphaCutoff;
} pc;
#else
uniform mat4 uBoneMatrices[MAX_BONES];
#endif

layout(location = 0) out vec3 vFragPos;   // world-space position
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out mat3 vTBN;       // 3 consecutive locations (2, 3, 4).

void main() {
    // Blend up to 4 bone transforms weighted by the per-vertex weights.
    // Each uBoneMatrices[i] = boneWorldMatrix * inverseBindMatrix, so the
    // blended result takes the vertex directly to world space.
    mat4 skinMatrix = aBoneWeights.x * uBoneMatrices[aBoneIndices.x]
                    + aBoneWeights.y * uBoneMatrices[aBoneIndices.y]
                    + aBoneWeights.z * uBoneMatrices[aBoneIndices.z]
                    + aBoneWeights.w * uBoneMatrices[aBoneIndices.w];

    vec4 worldPos  = skinMatrix * vec4(aPosition, 1.0);
    gl_Position    = uProjection * uView * worldPos;
    vFragPos       = worldPos.xyz;
    vTexCoord      = aTexCoord;

    mat3 normalMat = mat3(transpose(inverse(skinMatrix)));
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(mat3(skinMatrix) * aTangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = normalize(mat3(skinMatrix) * aBiTangent);
    vTBN = mat3(T, B, N);
}
