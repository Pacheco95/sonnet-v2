#version 330 core
in  vec3 vViewNormal;
out vec4 fragColor;

void main() {
    // Store view-space normal directly (RGBA16F supports negative values).
    fragColor = vec4(normalize(vViewNormal), 1.0);
}
