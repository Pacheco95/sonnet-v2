#version 460 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPosition.xy, 0.0, 1.0);
    vUV = aTexCoord;
}
