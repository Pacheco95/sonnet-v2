#version 330 core
// Vertex layout: position(0), texcoord(2), normal(3), tangent(4), bitangent(5)
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec3 aTangent;
layout(location = 5) in vec3 aBiTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vFragPos;  // world-space position
out vec2 vTexCoord;
out mat3 vTBN;      // TBN in world space (for normal mapping)

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
