// Sonnet v2 — Phase 15 demo
// ImGui debug panel (Tab to toggle); Scene graph drives the render queue.

#include <sonnet/api/render/Light.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/loaders/ModelLoader.h>
#include <sonnet/loaders/TextureLoader.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/ui/ImGuiLayer.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>
#include <sonnet/world/Scene.h>

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// ── Embedded shaders ──────────────────────────────────────────────────────────
// Vertex layout: position(0), texcoord(2), normal(3)

static constexpr const char *VERT_SRC = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec3 vFragPos;
out vec2 vTexCoord;

void main() {
    vec4 worldPos   = uModel * vec4(aPosition, 1.0);
    gl_Position     = uProjection * uView * worldPos;
    vFragPos        = worldPos.xyz;
    vNormal         = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord       = aTexCoord;
}
)glsl";

static constexpr const char *FRAG_SRC = R"glsl(
#version 330 core
in  vec3 vNormal;
in  vec3 vFragPos;
in  vec2 vTexCoord;
out vec4 fragColor;

struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};
uniform DirLight  uDirLight;
uniform sampler2D uAlbedo;

void main() {
    vec3  n       = normalize(vNormal);
    float diff    = max(dot(n, normalize(uDirLight.direction)), 0.0);
    vec3  albedo  = texture(uAlbedo, vTexCoord).rgb;
    vec3  col     = (0.15 + diff * uDirLight.intensity) * uDirLight.color * albedo;
    fragColor     = vec4(col, 1.0);
}
)glsl";

// ── Fly camera ────────────────────────────────────────────────────────────────

class FlyCamera {
public:
    FlyCamera() { update(0.0f, {}, {}); }

    void update(float dt,
                const sonnet::input::InputSystem &input,
                const glm::uvec2 &viewport) {
        using sonnet::api::input::Key;

        const glm::vec2 delta = input.mouseDelta();
        m_yaw   += delta.x * m_sensitivity;
        m_pitch  = std::clamp(m_pitch - delta.y * m_sensitivity, -89.0f, 89.0f);

        const float yr = glm::radians(m_yaw);
        const float pr = glm::radians(m_pitch);
        m_front = glm::normalize(glm::vec3{
            std::cos(yr) * std::cos(pr),
            std::sin(pr),
            std::sin(yr) * std::cos(pr),
        });
        const glm::vec3 right = glm::normalize(glm::cross(m_front, WORLD_UP));

        const float speed = m_speed * dt;
        if (input.isKeyDown(Key::W)) m_pos += m_front  * speed;
        if (input.isKeyDown(Key::S)) m_pos -= m_front  * speed;
        if (input.isKeyDown(Key::D)) m_pos += right     * speed;
        if (input.isKeyDown(Key::A)) m_pos -= right     * speed;
        if (input.isKeyDown(Key::E)) m_pos += WORLD_UP * speed;
        if (input.isKeyDown(Key::Q)) m_pos -= WORLD_UP * speed;

        m_view = glm::lookAt(m_pos, m_pos + m_front, WORLD_UP);
        const float aspect = viewport.x > 0 && viewport.y > 0
            ? static_cast<float>(viewport.x) / static_cast<float>(viewport.y)
            : 16.0f / 9.0f;
        m_proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 200.0f);
    }

    [[nodiscard]] const glm::mat4 &view() const { return m_view; }
    [[nodiscard]] const glm::mat4 &proj() const { return m_proj; }
    [[nodiscard]] const glm::vec3 &pos()  const { return m_pos; }

private:
    static constexpr glm::vec3 WORLD_UP{0, 1, 0};
    glm::vec3 m_pos{0.0f, 0.0f, 3.0f};
    float     m_yaw{-90.0f}, m_pitch{0.0f};
    float     m_speed{5.0f}, m_sensitivity{0.1f};
    glm::vec3 m_front{0, 0, -1};
    glm::mat4 m_view{1.0f}, m_proj{1.0f};
};

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    sonnet::window::GLFWWindow window{{1280, 720, "Sonnet v2 Demo"}};
    sonnet::input::InputSystem input;
    sonnet::window::GLFWInputAdapter adapter{input};
    window.setInputAdapter(&adapter);
    window.captureCursor();

    sonnet::renderer::opengl::GlRendererBackend backend;
    backend.initialize();

    sonnet::ui::ImGuiLayer imgui;
    imgui.init(window.handle());

    sonnet::renderer::frontend::Renderer renderer{backend};

    // Load mesh from OBJ via ModelLoader.
    const auto loadedMeshes = sonnet::loaders::ModelLoader::load(DEMO_ASSETS_DIR "/cube.obj");
    const auto meshHandle   = renderer.createMesh(loadedMeshes[0]);
    const auto shaderHandle = renderer.createShader(VERT_SRC, FRAG_SRC);

    const auto matHandle = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = shaderHandle,
        .renderState  = {},
    });

    // Load checkerboard texture via TextureLoader.
    const auto cpuTex  = sonnet::loaders::TextureLoader::load(DEMO_ASSETS_DIR "/checkerboard.png");
    const auto texDesc = sonnet::api::render::TextureDesc{
        .size       = {cpuTex.width, cpuTex.height},
        .format     = cpuTex.channels == 4 ? sonnet::api::render::TextureFormat::RGBA8
                                           : sonnet::api::render::TextureFormat::RGB8,
        .colorSpace = sonnet::api::render::ColorSpace::sRGB,
    };
    const auto texHandle = renderer.createTexture(texDesc, {}, cpuTex);

    // Build material instance (shared across frames; texture slot is constant).
    sonnet::api::render::MaterialInstance cubeMat{matHandle};
    cubeMat.addTexture("uAlbedo", texHandle);

    // Scene setup.
    sonnet::world::Scene scene;
    auto &cube = scene.createObject("Cube");
    cube.render = sonnet::world::RenderComponent{
        .mesh     = meshHandle,
        .material = cubeMat,
    };

    // Tweakable state exposed via ImGui.
    float              rotationSpeed  = 45.0f;
    glm::vec3          lightDir       = {0.6f, 1.0f, 0.4f};
    glm::vec3          lightColor     = {1.0f, 1.0f, 1.0f};
    float              lightIntensity = 1.0f;
    bool               uiMode         = false;  // Tab toggles cursor capture

    FlyCamera camera;
    float rotation = 0.0f;
    double prevTime = glfwGetTime();

    while (!window.shouldClose()) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape))
            window.requestClose();

        // Tab toggles between fly-camera mode and UI mode.
        if (input.isKeyJustPressed(sonnet::api::input::Key::Tab)) {
            uiMode = !uiMode;
            if (uiMode) window.releaseCursor();
            else        window.captureCursor();
        }

        const auto fbSize = window.getFrameBufferSize();

        // Only move the camera when the cursor is captured.
        if (!uiMode)
            camera.update(dt, input, fbSize);

        rotation += rotationSpeed * dt;
        cube.transform.setLocalRotation(
            glm::angleAxis(glm::radians(rotation),        glm::vec3{0, 1, 0}) *
            glm::angleAxis(glm::radians(rotation * 0.3f), glm::vec3{1, 0, 0})
        );

        backend.bindDefaultRenderTarget();
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({
            .colors = {{0, {0.1f, 0.1f, 0.15f, 1.0f}}},
            .depth  = 1.0f,
        });

        sonnet::api::render::FrameContext ctx{
            .viewMatrix       = camera.view(),
            .projectionMatrix = camera.proj(),
            .viewPosition     = camera.pos(),
            .viewportWidth    = fbSize.x,
            .viewportHeight   = fbSize.y,
            .deltaTime        = dt,
            .directionalLight = sonnet::api::render::DirectionalLight{
                .direction = lightDir,
                .color     = lightColor,
                .intensity = lightIntensity,
            },
        };

        std::vector<sonnet::api::render::RenderItem> queue;
        scene.buildRenderQueue(queue);

        renderer.beginFrame();
        renderer.render(ctx, queue);
        renderer.endFrame();

        // ── ImGui ──────────────────────────────────────────────────────────────
        imgui.begin();
        if (uiMode) {
            ImGui::Begin("Debug  [Tab to close]");

            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Direction",  &lightDir.x,   0.01f, -1.0f, 1.0f);
                ImGui::ColorEdit3("Color",      &lightColor.x);
                ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, 4.0f);
            }

            if (ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Rotation speed (°/s)", &rotationSpeed, 0.0f, 360.0f);
            }

            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                const glm::vec3 &p = camera.pos();
                ImGui::Text("Position  %.2f  %.2f  %.2f", p.x, p.y, p.z);
                ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
            }

            ImGui::End();
        }
        imgui.end();

        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
