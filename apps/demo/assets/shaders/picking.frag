#version 460 core
layout(location = 0) out vec4 fragColor;

#ifdef VULKAN
layout(push_constant) uniform Push {
    mat4 uModel;       // vertex consumes
    vec3 uPickColor;
} pc;
#define uPickColor pc.uPickColor
#else
uniform vec3 uPickColor;
#endif

void main() { fragColor = vec4(uPickColor, 1.0); }
