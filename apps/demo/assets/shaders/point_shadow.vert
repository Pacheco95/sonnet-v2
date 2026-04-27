#version 460 core
layout(location = 0) in vec3 aPosition;

// Camera UBO (set=0, binding=0) — uView is the per-face view matrix that
// ShadowMaps::render rebinds before each face. Same convention as the rest of
// the engine: every shader pulls camera matrices from this UBO under both
// backends.
layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

#ifdef VULKAN
layout(push_constant) uniform Push { mat4 uModel; } pc;
#define uModel pc.uModel
#else
uniform mat4 uModel;
#endif

out vec3 vFragPos;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vFragPos      = worldPos.xyz;
    gl_Position   = uProjection * uView * worldPos;
}
