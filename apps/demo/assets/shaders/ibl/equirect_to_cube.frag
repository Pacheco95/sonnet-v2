#version 460 core
in  vec3 vLocalPos;
out vec4 fragColor;

uniform sampler2D uEquirectMap;

const vec2 INV_ATAN = vec2(0.1591, 0.3183); // (1/2π, 1/π)

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(clamp(v.y, -1.0, 1.0)));
    uv *= INV_ATAN;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv    = sampleSphericalMap(normalize(vLocalPos));
    vec3 color = texture(uEquirectMap, uv).rgb;
    fragColor  = vec4(color, 1.0);
}
