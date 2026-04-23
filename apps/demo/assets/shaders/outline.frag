#version 460 core
in  vec2 vUV;
out vec4 fragColor;

layout(SET(1,0)) uniform sampler2D uMask;

#ifdef VULKAN
layout(push_constant) uniform Push {
    vec3 uOutlineColor;
} pc;
#define uOutlineColor pc.uOutlineColor
#else
uniform vec3 uOutlineColor;
#endif

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uMask, 0));

    float center = texture(uMask, vUV).r;

    // Dilate: find the maximum mask value within a 2-pixel radius.
    float maxNeighbor = 0.0;
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            if (dx == 0 && dy == 0) continue;
            maxNeighbor = max(maxNeighbor,
                texture(uMask, vUV + vec2(dx, dy) * texel).r);
        }
    }

    // Edge: pixel is outside the object but a neighbor is inside.
    float edge = (1.0 - center) * maxNeighbor;
    fragColor = vec4(uOutlineColor, edge);
}
