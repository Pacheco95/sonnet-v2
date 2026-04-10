#version 330 core
in  vec3 vNormal;
in  vec3 vFragPos;
in  vec2 vTexCoord;
in  vec4 vLightSpacePos;
out vec4 fragColor;

struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};
uniform DirLight        uDirLight;
uniform sampler2D       uAlbedo;
uniform sampler2DShadow uShadowMap;
uniform float           uShadowBias;

// 3x3 PCF shadow factor using hardware depth comparison.
// Each texture() call on a sampler2DShadow returns a bilinearly filtered
// [0,1] result: 1.0 = lit, 0.0 = occluded.
float shadowFactor(vec3 n) {
    vec3 proj = vLightSpacePos.xyz / vLightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    // Outside the shadow frustum -> fully lit
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
                        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = max(uShadowBias * (1.0 - dot(n, normalize(uDirLight.direction))),
                     uShadowBias * 0.1);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(uShadowMap, vec3(proj.xy + vec2(x, y) * texelSize, proj.z - bias));
        }
    return shadow / 9.0;
}

void main() {
    vec3  n      = normalize(vNormal);
    float diff   = max(dot(n, normalize(uDirLight.direction)), 0.0);
    vec3  albedo = texture(uAlbedo, vTexCoord).rgb;
    float shadow = shadowFactor(n);
    vec3  col    = (0.15 + diff * uDirLight.intensity * shadow) * uDirLight.color * albedo;
    fragColor    = vec4(col, 1.0);
}
