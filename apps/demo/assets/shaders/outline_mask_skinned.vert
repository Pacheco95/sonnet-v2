#version 460 core
layout(location = 0) in vec3  aPosition;
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

#ifdef VULKAN
layout(std140, SET(2,0)) uniform PerDraw {
    mat4 uBoneMatrices[MAX_BONES];
} pd;
#define uBoneMatrices pd.uBoneMatrices
#else
uniform mat4 uBoneMatrices[MAX_BONES];
#endif

void main() {
    mat4 skinMatrix = aBoneWeights.x * uBoneMatrices[aBoneIndices.x]
                    + aBoneWeights.y * uBoneMatrices[aBoneIndices.y]
                    + aBoneWeights.z * uBoneMatrices[aBoneIndices.z]
                    + aBoneWeights.w * uBoneMatrices[aBoneIndices.w];
    gl_Position = uProjection * uView * skinMatrix * vec4(aPosition, 1.0);
}
