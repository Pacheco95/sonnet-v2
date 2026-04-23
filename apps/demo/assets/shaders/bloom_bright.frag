#version 460 core
in  vec2 vUV;
out vec4 fragColor;

layout(SET(1,0)) uniform sampler2D uHdrColor;

#ifdef VULKAN
layout(push_constant) uniform Push {
    float uBloomThreshold;
} pc;
#define uBloomThreshold pc.uBloomThreshold
#else
uniform float uBloomThreshold;
#endif

void main() {
    vec3  color      = texture(uHdrColor, vUV).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fragColor = brightness > uBloomThreshold ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
