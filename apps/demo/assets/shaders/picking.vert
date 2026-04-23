#version 460 core
layout(location = 0) in vec3 aPosition;

layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4 uModel;
    vec3 uPickColor;   // fragment uses this
} pc;
#define uModel pc.uModel
#else
uniform mat4 uModel;
#endif

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
