#version 460 core
in  vec2 vUV;
out vec4 fragColor;

layout(SET(1,0)) uniform sampler2D uSSAOTexture;

void main() {
    // Simple 5x5 box blur to smooth SSAO noise.
    vec2  texelSize = 1.0 / vec2(textureSize(uSSAOTexture, 0));
    float result    = 0.0;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result     += texture(uSSAOTexture, vUV + offset).r;
        }
    }
    fragColor = vec4(result / 25.0, 0.0, 0.0, 1.0);
}
