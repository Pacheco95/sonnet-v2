#version 460 core
in  vec2 vUV;
out vec4 fragColor; // rgb = reflection colour, a = unused

uniform sampler2D uDepth;           // G-buffer depth [0,1]
uniform sampler2D uNormalMetallic;  // G-buffer attachment 1: world normal.rgb + metallic.a
uniform sampler2D uAlbedoRoughness; // G-buffer attachment 0: albedo.rgb + roughness.a
uniform sampler2D uHDRColor;        // HDR lit result — the reflection source

uniform mat4  uProjection;
uniform mat4  uInvProjection;
uniform mat4  uView;
uniform vec2  uResolution;          // framebuffer size in pixels

uniform int   uMaxSteps;            // max ray steps (32–128)
uniform float uStepSize;            // initial step length in view-space units
uniform float uThickness;           // depth-test tolerance (view-space)
uniform float uMaxDistance;         // ray travel cutoff (view-space)
uniform float uRoughnessMax;        // fragments rougher than this are skipped

// Reconstruct view-space position from a screen UV and a raw depth value.
vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 ndcPos  = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = uInvProjection * ndcPos;
    return viewPos.xyz / viewPos.w;
}

// Project a view-space position to screen-space UV.
vec2 projectToUV(vec3 vp) {
    vec4 clip = uProjection * vec4(vp, 1.0);
    return (clip.xy / clip.w) * 0.5 + 0.5;
}

void main() {
    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0) { fragColor = vec4(0.0); return; } // sky pixel — nothing to reflect

    float roughness = texture(uAlbedoRoughness, vUV).a;
    if (roughness > uRoughnessMax) { fragColor = vec4(0.0); return; } // too rough

    vec4  normalMet  = texture(uNormalMetallic, vUV);
    float metallic   = normalMet.a;

    // Fresnel-approximate reflectivity: dielectrics use F0=0.04, metals use full colour.
    // Here we just use it as a scalar blend weight.
    float reflectivity = mix(0.04, 1.0, metallic) * (1.0 - roughness);

    // Reconstruct view-space position and transform world normal to view space.
    vec3 viewPos    = viewPosFromDepth(vUV, depth);
    vec3 worldNorm  = normalize(normalMet.rgb);
    vec3 viewNorm   = normalize(mat3(uView) * worldNorm);

    // Reflection direction in view space (camera is at origin).
    vec3 viewDir = normalize(viewPos);
    vec3 reflDir = reflect(viewDir, viewNorm);

    // Ray march.
    vec3  rayPos  = viewPos;
    float stepLen = uStepSize;
    vec4  result  = vec4(0.0);

    for (int i = 0; i < uMaxSteps; ++i) {
        rayPos  += reflDir * stepLen;
        stepLen *= 1.05; // exponential growth keeps close detail sharp

        if (rayPos.z >= 0.0) break;                              // behind camera
        if (length(rayPos - viewPos) > uMaxDistance) break;      // too far

        vec2 rayUV = projectToUV(rayPos);
        if (any(lessThan(rayUV, vec2(0.0))) || any(greaterThan(rayUV, vec2(1.0)))) break;

        float sampleDepth = texture(uDepth, rayUV).r;
        if (sampleDepth >= 1.0) continue; // hit sky, keep going

        float surfZ = viewPosFromDepth(rayUV, sampleDepth).z;

        // Intersection: ray crossed to the far side of a surface within thickness.
        if (rayPos.z < surfZ && rayPos.z > surfZ - uThickness) {
            // Fade at screen edges and with ray distance.
            vec2  ef       = smoothstep(0.0, 0.1, rayUV) * (1.0 - smoothstep(0.9, 1.0, rayUV));
            float edgeFade = ef.x * ef.y;
            float distFade = 1.0 - clamp(length(rayPos - viewPos) / uMaxDistance, 0.0, 1.0);

            vec3 hitColor = texture(uHDRColor, rayUV).rgb;
            result = vec4(hitColor * reflectivity * edgeFade * distFade, 1.0);
            break;
        }
    }

    fragColor = result;
}
