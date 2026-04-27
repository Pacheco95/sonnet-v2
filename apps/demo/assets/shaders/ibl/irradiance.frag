#version 460 core
in  vec3 vLocalPos;
out vec4 fragColor;

layout(SET(1,0)) uniform samplerCube uEnvMap;

const float PI = 3.14159265359;

void main() {
    // The normal is the direction we're computing irradiance for.
    vec3 N = normalize(vLocalPos);

    // Build a tangent-space basis around N.
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    vec3  irradiance = vec3(0.0);
    float nrSamples  = 0.0;

    // Uniform hemisphere sampling (Riemann integral).
    const float sampleDelta = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // Spherical to cartesian (in tangent space).
            vec3 tangentSample = vec3(sin(theta) * cos(phi),
                                      sin(theta) * sin(phi),
                                      cos(theta));
            // Tangent space to world space.
            vec3 sampleVec = tangentSample.x * right
                           + tangentSample.y * up
                           + tangentSample.z * N;

            irradiance += texture(uEnvMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }

    irradiance = PI * irradiance / nrSamples;
    fragColor  = vec4(irradiance, 1.0);
}
