#version 460 core
layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec3 aNormal;

layout(std140, binding = 0) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

uniform mat4 uModel;

out vec3 vViewNormal;

void main() {
    // Normal in view space (uses inverse-transpose for non-uniform scale).
    mat3 normalMat = mat3(transpose(inverse(uView * uModel)));
    vViewNormal    = normalize(normalMat * aNormal);
    gl_Position    = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
