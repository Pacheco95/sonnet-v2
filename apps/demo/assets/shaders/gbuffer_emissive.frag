#version 460 core
// G-buffer fragment shader for purely emissive objects (e.g. lamp sphere).
// Writes zero albedo/roughness and the emissive colour into the G-buffer;
// the deferred lighting pass will add the emissive directly to the final colour.
layout(location = 0) out vec4 gAlbedoRoughness;
layout(location = 1) out vec4 gNormalMetallic;
layout(location = 2) out vec4 gEmissiveAO;

uniform vec3  uEmissiveColor;
uniform float uEmissiveStrength;

void main() {
    gAlbedoRoughness = vec4(0.0);
    gNormalMetallic  = vec4(0.0, 1.0, 0.0, 0.0); // dummy up-normal, metallic=0
    gEmissiveAO      = vec4(uEmissiveColor * uEmissiveStrength, 1.0);
}
