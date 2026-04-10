#version 330 core
// Vertex layout: position(0), texcoord(2), normal(3)
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpaceMatrix;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;
out vec4 vLightSpacePos;

void main() {
    vec4 worldPos    = uModel * vec4(aPosition, 1.0);
    gl_Position      = uProjection * uView * worldPos;
    vFragPos         = worldPos.xyz;
    vNormal          = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord        = aTexCoord;
    vLightSpacePos   = uLightSpaceMatrix * worldPos;
}
