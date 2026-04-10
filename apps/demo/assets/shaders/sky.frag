#version 330 core
in  vec3 vDir;
out vec4 fragColor;

uniform sampler2D uEnvMap; // equirectangular HDR

const float PI = 3.14159265359;

vec2 dirToUV(vec3 d) {
    float phi   = atan(d.z, d.x);                 // -π .. π
    float theta = asin(clamp(d.y, -1.0, 1.0));    // -π/2 .. π/2
    return vec2(phi / (2.0 * PI) + 0.5,
                theta / PI + 0.5);
}

void main() {
    vec2 uv   = dirToUV(normalize(vDir));
    fragColor = vec4(texture(uEnvMap, uv).rgb, 1.0);
}
