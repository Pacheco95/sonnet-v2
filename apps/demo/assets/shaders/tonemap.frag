#version 460 core
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(SET(1,0)) uniform sampler2D uHdrColor;
layout(SET(1,1)) uniform sampler2D uBloomTexture;
layout(SET(1,2)) uniform sampler2D uSSRTex;

#ifdef VULKAN
layout(push_constant) uniform Push {
    float uExposure;
    float uBloomIntensity;
    float uSSRStrength;
} pc;
#define uExposure       pc.uExposure
#define uBloomIntensity pc.uBloomIntensity
#define uSSRStrength    pc.uSSRStrength
#else
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uSSRStrength;
#endif

// ACES filmic tone-mapping approximation (Krzysztof Narkowicz)
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr  = texture(uHdrColor, vUV).rgb
              + texture(uBloomTexture, vUV).rgb * uBloomIntensity
              + texture(uSSRTex,      vUV).rgb * uSSRStrength;
    fragColor = vec4(aces(hdr * uExposure), 1.0);
}
