#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vViewNormal;

void main() {
    // Normal in view space (uses inverse-transpose for non-uniform scale).
    mat3 normalMat = mat3(transpose(inverse(uView * uModel)));
    vViewNormal    = normalize(normalMat * aNormal);
    gl_Position    = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
