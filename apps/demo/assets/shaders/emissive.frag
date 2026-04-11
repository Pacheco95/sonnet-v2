#version 330 core
out vec4 fragColor;

uniform vec3  uEmissiveColor;    // linear HDR color (values > 1.0 will bloom)
uniform float uEmissiveStrength; // multiplier — set > 1.0 to exceed bloom threshold

void main() {
    fragColor = vec4(uEmissiveColor * uEmissiveStrength, 1.0);
}
