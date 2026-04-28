#version 460 core
layout(location = 0) out vec4 fragColor;

// Mirrors the push-constant block in emissive.vert. Vulkan requires the
// declared block to be identical across stages when the engine merges ranges
// via SPIRV-Reflect, so we re-declare all members here even though only two
// are consumed in this stage.
#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4  uModel;
    vec3  uEmissiveColor;
    float uEmissiveStrength;
} pc;
#define uEmissiveColor    pc.uEmissiveColor
#define uEmissiveStrength pc.uEmissiveStrength
#else
uniform vec3  uEmissiveColor;    // linear HDR color (values > 1.0 will bloom)
uniform float uEmissiveStrength; // multiplier — set > 1.0 to exceed bloom threshold
#endif

void main() {
    fragColor = vec4(uEmissiveColor * uEmissiveStrength, 1.0);
}
