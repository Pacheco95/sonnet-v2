#version 330 core
in  vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSSAO; // blurred AO (R channel, [0=fully occluded, 1=no AO])

void main() {
    float ao = texture(uSSAO, vUV).r;
    fragColor = vec4(ao, ao, ao, 1.0);
}
