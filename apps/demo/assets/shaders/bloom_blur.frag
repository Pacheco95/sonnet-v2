#version 460 core
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(SET(1,0)) uniform sampler2D uBloomTexture;

#ifdef VULKAN
layout(push_constant) uniform Push {
    int uHorizontal; // 1 = horizontal, 0 = vertical
} pc;
#define uHorizontal pc.uHorizontal
#else
uniform int uHorizontal; // 1 = horizontal, 0 = vertical
#endif

// 9-tap separable Gaussian weights (sigma ≈ 2.0).
const float WEIGHT[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(uBloomTexture, 0));
    vec3 result    = texture(uBloomTexture, vUV).rgb * WEIGHT[0];

    if (uHorizontal != 0) {
        for (int i = 1; i < 5; ++i) {
            result += texture(uBloomTexture, vUV + vec2(texelSize.x * i, 0.0)).rgb * WEIGHT[i];
            result += texture(uBloomTexture, vUV - vec2(texelSize.x * i, 0.0)).rgb * WEIGHT[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(uBloomTexture, vUV + vec2(0.0, texelSize.y * i)).rgb * WEIGHT[i];
            result += texture(uBloomTexture, vUV - vec2(0.0, texelSize.y * i)).rgb * WEIGHT[i];
        }
    }

    fragColor = vec4(result, 1.0);
}
