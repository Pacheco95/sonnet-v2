#version 460 core
// G-buffer fragment shader for purely emissive objects (e.g. lamp sphere).
// Writes zero albedo/roughness and the emissive colour into the G-buffer;
// the deferred lighting pass will add the emissive directly to the final colour.
layout(location = 0) out vec4 gAlbedoRoughness;
layout(location = 1) out vec4 gNormalMetallic;
layout(location = 2) out vec4 gEmissiveAO;

// Mirrors emissive.vert/frag push layout so the engine's Renderer can bind a
// single MaterialInstance targeting this shader with uEmissiveColor/Strength.
#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4  uModel;
    vec3  uEmissiveColor;
    float uEmissiveStrength;
} pc;
#define uEmissiveColor    pc.uEmissiveColor
#define uEmissiveStrength pc.uEmissiveStrength
#else
uniform vec3  uEmissiveColor;
uniform float uEmissiveStrength;
#endif

void main() {
    gAlbedoRoughness = vec4(0.0);
    gNormalMetallic  = vec4(0.0, 1.0, 0.0, 0.0); // dummy up-normal, metallic=0
    gEmissiveAO      = vec4(uEmissiveColor * uEmissiveStrength, 1.0);
}
