#version 460 core
layout(location = 0) in vec3 aPosition;

// Camera UBO at set=0, binding=0 — matches the rest of the engine. IBL.h
// fills uView (per face) and uProjection (CAPTURE_PROJ, fixed) via the
// engine's FrameContext when calling Renderer::render() per face.
layout(std140, SET(0,0)) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

out vec3 vLocalPos;

void main() {
    vLocalPos   = aPosition;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
