#version 330 core
uniform vec3 uPickColor;
out vec4 fragColor;
void main() { fragColor = vec4(uPickColor, 1.0); }
