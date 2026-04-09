// Sonnet v2 — Phase 11 demo
// Renders a rotating box with Blinn-Phong directional lighting and a fly camera (WASD + mouse).

#include <sonnet/api/render/Light.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/api/render/RenderItem.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/primitives/MeshPrimitives.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>

#include <glm/gtc/matrix_transform.hpp>

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

void main() {
    vec4 worldPos   = uModel * vec4(aPosition, 1.0);
    gl_Position     = uProjection * uView * worldPos;
    vFragPos        = worldPos.xyz;
    vNormal         = mat3(transpose(inverse(uModel))) * aNormal;
}
)glsl";

static constexpr const char *FRAG_SRC = R"glsl(
#version 330 core
in  vec3 vNormal;
in  vec3 vFragPos;
out vec4 fragColor;

struct DirLight {
    vec3  direction;
    vec3  color;
    float intensity;
};
uniform DirLight uDirLight;
uniform vec3     uObjectColor;

void main() {
    vec3  n    = normalize(vNormal);
    float diff = max(dot(n, normalize(uDirLight.direction)), 0.0);
    vec3  col  = (0.15 + diff * uDirLight.intensity) * uDirLight.color * uObjectColor;
    fragColor  = vec4(col, 1.0);
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

    sonnet::renderer::frontend::Renderer renderer{backend};

    // Box from primitives module (Position + TexCoord + Normal).
    const auto meshHandle   = renderer.createMesh(sonnet::primitives::makeBox({1.0f, 1.0f, 1.0f}));
    const auto shaderHandle = renderer.createShader(VERT_SRC, FRAG_SRC);

    const auto matHandle = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = shaderHandle,
        .renderState  = {},
    });

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

        const auto fbSize = window.getFrameBufferSize();
        camera.update(dt, input, fbSize);
        rotation += 45.0f * dt;

        backend.bindDefaultRenderTarget();
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({
            .colors = {{0, {0.1f, 0.1f, 0.15f, 1.0f}}},
            .depth  = 1.0f,
        });

        glm::mat4 model{1.0f};
        model = glm::rotate(model, glm::radians(rotation),        {0, 1, 0});
        model = glm::rotate(model, glm::radians(rotation * 0.3f), {1, 0, 0});

        sonnet::api::render::FrameContext ctx{
            .viewMatrix       = camera.view(),
            .projectionMatrix = camera.proj(),
            .viewPosition     = camera.pos(),
            .viewportWidth    = fbSize.x,
            .viewportHeight   = fbSize.y,
            .deltaTime        = dt,
            .directionalLight = sonnet::api::render::DirectionalLight{
                .direction = {0.6f, 1.0f, 0.4f},
                .color     = {1.0f, 1.0f, 1.0f},
                .intensity = 1.0f,
            },
        };

        // Material instance with per-object color.
        sonnet::api::render::MaterialInstance mat{matHandle};
        mat.set("uObjectColor", glm::vec3{0.4f, 0.7f, 1.0f});

        std::vector<sonnet::api::render::RenderItem> queue;
        queue.push_back({
            .mesh        = meshHandle,
            .material    = mat,
            .modelMatrix = model,
        });

        renderer.beginFrame();
        renderer.render(ctx, queue);
        renderer.endFrame();

        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
