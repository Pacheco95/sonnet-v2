#version 330 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uHdrColor;
uniform sampler2D uBloomTexture;
uniform sampler2D uSSRTex;
uniform float     uExposure;
uniform float     uBloomIntensity;
uniform float     uSSRStrength;

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
