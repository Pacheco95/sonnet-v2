#version 460 core
// Vertex layout: position(0), texcoord(2), normal(3), tangent(4), bitangent(5)
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec3 aTangent;
layout(location = 5) in vec3 aBiTangent;

layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

// Shared push-constant block with gbuffer.frag. Fits all per-draw material
// scalars + uModel in 116 bytes, under the 128-byte spec minimum.
#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4  uModel;           // 0
    vec3  uEmissiveFactor;  // 64  (vec3 padded to vec4 slot in std140 push-layout)
    float uMetallic;        // 80
    float uRoughness;       // 84
    vec4  uAlbedoFactor;    // 96  (aligned to 16)
    float uAlphaCutoff;     // 112
} pc;
#define uModel pc.uModel
#else
uniform mat4 uModel;
#endif

layout(location = 0) out vec3 vFragPos;  // world-space position
layout(location = 1) out vec2 vTexCoord;
// mat3 spans 3 consecutive locations under Vulkan's strict location rules.
layout(location = 2) out mat3 vTBN;      // TBN in world space (for normal mapping)

void main() {
    vec4 worldPos  = uModel * vec4(aPosition, 1.0);
    gl_Position    = uProjection * uView * worldPos;
    vFragPos       = worldPos.xyz;
    vTexCoord      = aTexCoord;

    mat3 normalMat = mat3(transpose(inverse(uModel)));
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(mat3(uModel) * aTangent);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt re-orthogonalise
    vec3 B = normalize(mat3(uModel) * aBiTangent);
    vTBN = mat3(T, B, N);
}
