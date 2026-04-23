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
uniform mat4 uBoneMatrices[MAX_BONES];

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vFragPos;   // world-space position
out vec2 vTexCoord;
out mat3 vTBN;       // TBN in world space (for normal mapping)

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
