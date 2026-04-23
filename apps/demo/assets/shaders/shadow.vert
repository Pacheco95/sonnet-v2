#version 460 core
layout(location = 0) in vec3 aPosition;

// Shadow passes use the light's view/proj (different from Camera UBO), so all
// three matrices travel per-draw. Too big for push constants — 192 bytes — so
// they live in the set=2 PerDraw UBO under Vulkan.
#ifdef VULKAN
layout(std140, SET(2,0)) uniform PerDraw {
    mat4 uView;
    mat4 uProjection;
    mat4 uModel;
} pd;
#define uView       pd.uView
#define uProjection pd.uProjection
#define uModel      pd.uModel
#else
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uModel;
#endif

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
