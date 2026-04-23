#version 460 core
// Vertex layout: position(0), texcoord(2), normal(3), tangent(4), bitangent(5)
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec3 aTangent;
layout(location = 5) in vec3 aBiTangent;

layout(std140, binding = 0) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;

out vec3 vFragPos;
out vec2 vTexCoord;
out vec4 vLightSpacePos;
out mat3 vTBN;

void main() {
    vec4 worldPos  = uModel * vec4(aPosition, 1.0);
    gl_Position    = uProjection * uView * worldPos;
    vFragPos       = worldPos.xyz;
    vTexCoord      = aTexCoord;
    vLightSpacePos = uLightSpaceMatrix * worldPos;

    // Build TBN matrix in world space.
    // Tangents transform like positions (mat3(uModel)); normals need the inverse-transpose.
    mat3 normalMat = mat3(transpose(inverse(uModel)));
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(mat3(uModel) * aTangent);
    // Re-orthogonalise T against N (Gram-Schmidt) to remove any accumulated error.
    T = normalize(T - dot(T, N) * N);
    vec3 B = normalize(mat3(uModel) * aBiTangent);
    vTBN = mat3(T, B, N);
}
