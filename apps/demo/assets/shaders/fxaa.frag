#version 460 core
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(SET(1,0)) uniform sampler2D uScreen;
layout(SET(1,1)) uniform sampler2D uDepth;   // pre-pass depth — used to gate on geometric edges only

#ifdef VULKAN
layout(push_constant) uniform Push {
    vec2 uTexelSize;
} pc;
#define uTexelSize pc.uTexelSize
#else
uniform vec2 uTexelSize; // 1.0 / vec2(screenWidth, screenHeight)
#endif

// Luminance weights (Rec. 601)
const vec3 LUMA = vec3(0.299, 0.587, 0.114);

float luma(vec3 rgb) { return dot(rgb, LUMA); }

void main() {
    vec3 colorC = texture(uScreen, vUV).rgb;

    // ── Geometric edge guard ──────────────────────────────────────────────────
    // PBR shading produces large within-surface luminance variation (specular
    // highlights, normal-map detail) that naive luma-FXAA misidentifies as
    // edges and blurs.  Gate on a hardware-depth discontinuity: only pixels
    // where adjacent samples belong to *different geometry* are anti-aliased.
    float dC = texture(uDepth, vUV).r;
    float dN = textureOffset(uDepth, vUV, ivec2( 0,  1)).r;
    float dS = textureOffset(uDepth, vUV, ivec2( 0, -1)).r;
    float dE = textureOffset(uDepth, vUV, ivec2( 1,  0)).r;
    float dW = textureOffset(uDepth, vUV, ivec2(-1,  0)).r;
    float dMin = min(dC, min(min(dN, dS), min(dE, dW)));
    float dMax = max(dC, max(max(dN, dS), max(dE, dW)));
    if (dMax - dMin < 0.0005) {
        // No depth discontinuity — same geometry on all sides, pass through.
        fragColor = vec4(colorC, 1.0);
        return;
    }

    // ── Sample 4 cardinal + 4 diagonal neighbours ─────────────────────────────
    vec3 colorN  = textureOffset(uScreen, vUV, ivec2( 0,  1)).rgb;
    vec3 colorS  = textureOffset(uScreen, vUV, ivec2( 0, -1)).rgb;
    vec3 colorE  = textureOffset(uScreen, vUV, ivec2( 1,  0)).rgb;
    vec3 colorW  = textureOffset(uScreen, vUV, ivec2(-1,  0)).rgb;
    vec3 colorNE = textureOffset(uScreen, vUV, ivec2( 1,  1)).rgb;
    vec3 colorNW = textureOffset(uScreen, vUV, ivec2(-1,  1)).rgb;
    vec3 colorSE = textureOffset(uScreen, vUV, ivec2( 1, -1)).rgb;
    vec3 colorSW = textureOffset(uScreen, vUV, ivec2(-1, -1)).rgb;

    float lumC  = luma(colorC);
    float lumN  = luma(colorN);
    float lumS  = luma(colorS);
    float lumE  = luma(colorE);
    float lumW  = luma(colorW);
    float lumNE = luma(colorNE);
    float lumNW = luma(colorNW);
    float lumSE = luma(colorSE);
    float lumSW = luma(colorSW);

    float lumMin = min(lumC, min(min(lumN, lumS), min(lumE, lumW)));
    float lumMax = max(lumC, max(max(lumN, lumS), max(lumE, lumW)));
    float lumRange = lumMax - lumMin;

    // Skip pixels with no significant luminance contrast on the geometric edge.
    if (lumRange < max(0.0312, lumMax * 0.125)) {
        fragColor = vec4(colorC, 1.0);
        return;
    }

    // ── Classify edge direction ────────────────────────────────────────────────
    float vertical   = abs(lumNW + lumN + lumNE - lumSW - lumS - lumSE);
    float horizontal = abs(lumNW + lumW + lumSW - lumNE - lumE - lumSE);
    bool isHorizontal = vertical > horizontal;

    // Pick the two neighbours perpendicular to the edge.
    float lum1 = isHorizontal ? lumS : lumW;
    float lum2 = isHorizontal ? lumN : lumE;
    float gradient1 = abs(lum1 - lumC);
    float gradient2 = abs(lum2 - lumC);

    // Step direction: toward the steeper gradient.
    vec2 stepDir = isHorizontal ? vec2(0.0, uTexelSize.y) : vec2(uTexelSize.x, 0.0);
    if (gradient1 < gradient2) stepDir = -stepDir;

    // ── Blend ─────────────────────────────────────────────────────────────────
    // Subpixel blend factor from the full 3x3 average.
    float lumAvg = (lumN + lumS + lumE + lumW +
                    lumNE + lumNW + lumSE + lumSW) * (1.0 / 8.0);
    float subpixelBlend = clamp(abs(lumAvg - lumC) / lumRange, 0.0, 1.0);
    subpixelBlend = smoothstep(0.0, 1.0, subpixelBlend);
    subpixelBlend = subpixelBlend * subpixelBlend * 0.75;

    // Sample half a texel along the edge gradient and blend.
    vec3 colorBlend = texture(uScreen, vUV + stepDir * 0.5).rgb;
    fragColor = vec4(mix(colorC, colorBlend, subpixelBlend), 1.0);
}
