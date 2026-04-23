#version 460 core
in  vec3 vFragPos;
out vec4 fragColor;

uniform vec3  uLightPos;
uniform float uFarPlane;

void main() {
    // Store normalized linear distance [0,1] in both the colour attachment
    // (sampled during lighting) and gl_FragDepth (drives depth test so that
    // closer geometry wins within a face).
    float dist    = length(vFragPos - uLightPos) / uFarPlane;
    gl_FragDepth  = dist;
    fragColor     = vec4(dist, 0.0, 0.0, 1.0);
}
