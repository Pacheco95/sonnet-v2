#version 460 core
layout(location = 0) in vec3  aPosition;
layout(location = 6) in ivec4 aBoneIndices;
layout(location = 7) in vec4  aBoneWeights;

const int MAX_BONES = 128;
uniform mat4 uBoneMatrices[MAX_BONES];
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    mat4 skinMatrix = aBoneWeights.x * uBoneMatrices[aBoneIndices.x]
                    + aBoneWeights.y * uBoneMatrices[aBoneIndices.y]
                    + aBoneWeights.z * uBoneMatrices[aBoneIndices.z]
                    + aBoneWeights.w * uBoneMatrices[aBoneIndices.w];
    gl_Position = uProjection * uView * skinMatrix * vec4(aPosition, 1.0);
}
