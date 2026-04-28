#version 460 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord   = aTexCoord;
    gl_Position = vec4(aPosition.xy, 0.0, 1.0);
}
