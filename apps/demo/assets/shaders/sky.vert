#version 460 core
layout(location = 0) in vec3 aPosition;

layout(std140, binding = 0) uniform CameraUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPosition;
    mat4 uInvViewProj;
    mat4 uInvProjection;
};

out vec3 vDir;

void main() {
    // Clip at depth=1.0 so the sky renders behind all geometry.
    gl_Position = vec4(aPosition.xy, 1.0, 1.0);

    // Reconstruct world-space view direction.
    // Strip translation from view so the sky stays fixed as camera moves.
    mat4 rotView  = mat4(mat3(uView));
    mat4 invVP    = inverse(uProjection * rotView);
    vec4 worldDir = invVP * vec4(aPosition.xy, 0.0, 1.0);
    vDir          = worldDir.xyz / worldDir.w;
}
