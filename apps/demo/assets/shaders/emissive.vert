#version 460 core
layout(location = 0) in vec3 aPosition;

layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

// Per-draw data. Under Vulkan we pack these into a single push-constant block
// shared with emissive.frag (identical layout, different members consumed per
// stage). Under OpenGL each name becomes a plain default uniform so the existing
// setUniform call sites don't change.
#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4  uModel;
    vec3  uEmissiveColor;
    float uEmissiveStrength;
} pc;
#define uModel             pc.uModel
#define uEmissiveColor     pc.uEmissiveColor
#define uEmissiveStrength  pc.uEmissiveStrength
#else
uniform mat4  uModel;
uniform vec3  uEmissiveColor;
uniform float uEmissiveStrength;
#endif

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
