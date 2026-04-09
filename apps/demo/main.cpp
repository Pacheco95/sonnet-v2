// Sonnet v2 — Phase 9 demo
// Renders a rotating colored cube with a fly camera (WASD + mouse).

#include <sonnet/api/render/CPUMesh.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/api/render/RenderItem.h>
#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/renderer/opengl/GlRendererBackend.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ── Embedded shaders ──────────────────────────────────────────────────────────

static constexpr const char *VERT_SRC = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec4 vColor;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
    vColor = aColor;
}
)glsl";

static constexpr const char *FRAG_SRC = R"glsl(
#version 330 core
in  vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)glsl";

// ── Cube mesh builder ─────────────────────────────────────────────────────────

static sonnet::api::render::CPUMesh buildCube() {
    using namespace sonnet::api::render;

    // 8 corners: position maps directly to RGB color.
    struct Vert { glm::vec3 pos; glm::vec4 color; };
    static constexpr std::array<Vert, 8> verts{{
        {{-0.5f,-0.5f,-0.5f}, {0,0,0,1}},
        {{ 0.5f,-0.5f,-0.5f}, {1,0,0,1}},
        {{-0.5f,-0.5f, 0.5f}, {0,0,1,1}},
        {{ 0.5f,-0.5f, 0.5f}, {1,0,1,1}},
        {{-0.5f, 0.5f,-0.5f}, {0,1,0,1}},
        {{ 0.5f, 0.5f,-0.5f}, {1,1,0,1}},
        {{-0.5f, 0.5f, 0.5f}, {0,1,1,1}},
        {{ 0.5f, 0.5f, 0.5f}, {1,1,1,1}},
    }};

    static constexpr std::array<std::uint32_t, 36> idx{{
        // Front (+z)
        2,3,7, 2,7,6,
        // Back (-z)
        1,5,4, 1,4,0,
        // Right (+x)
        1,3,7, 1,7,5,
        // Left (-x)
        0,4,6, 0,6,2,
        // Top (+y)
        4,5,7, 4,7,6,
        // Bottom (-y)
        0,2,3, 0,3,1,
    }};

    KnownAttributeSet layout;
    layout.insert(PositionAttribute{});
    layout.insert(ColorAttribute{});

    CPUMesh mesh{VertexLayout{layout},
                 std::vector<CPUMesh::Index>(idx.begin(), idx.end()),
                 8};

    for (const auto &v : verts) {
        KnownAttributeSet attrs;
        attrs.insert(PositionAttribute{v.pos});
        attrs.insert(ColorAttribute{v.color});
        mesh.addVertex(attrs);
    }
    return mesh;
}

// ── Minimal fly camera ────────────────────────────────────────────────────────

class FlyCamera {
public:
    FlyCamera() { update(0.0f, {}, {}); }

    void update(float dt,
                const sonnet::input::InputSystem &input,
                const glm::uvec2 &viewport) {
        using sonnet::api::input::Key;

        // Mouse look.
        const glm::vec2 delta = input.mouseDelta();
        m_yaw   += delta.x * m_sensitivity;
        m_pitch  = std::clamp(m_pitch - delta.y * m_sensitivity, -89.0f, 89.0f);

        // Direction vectors.
        const float yr = glm::radians(m_yaw);
        const float pr = glm::radians(m_pitch);
        m_front = glm::normalize(glm::vec3{
            std::cos(yr) * std::cos(pr),
            std::sin(pr),
            std::sin(yr) * std::cos(pr),
        });
        const glm::vec3 right = glm::normalize(glm::cross(m_front, WORLD_UP));

        // WASD movement.
        const float speed = m_speed * dt;
        if (input.isKeyDown(Key::W)) m_pos += m_front  * speed;
        if (input.isKeyDown(Key::S)) m_pos -= m_front  * speed;
        if (input.isKeyDown(Key::D)) m_pos += right     * speed;
        if (input.isKeyDown(Key::A)) m_pos -= right     * speed;
        if (input.isKeyDown(Key::E)) m_pos += WORLD_UP * speed;
        if (input.isKeyDown(Key::Q)) m_pos -= WORLD_UP * speed;

        // Matrices.
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
    // Window + input.
    sonnet::window::GLFWWindow window{{1280, 720, "Sonnet v2 Demo"}};
    sonnet::input::InputSystem input;
    sonnet::window::GLFWInputAdapter adapter{input};
    window.setInputAdapter(&adapter);
    window.captureCursor();

    // Backend.
    sonnet::renderer::opengl::GlRendererBackend backend;
    backend.initialize();

    // Frontend renderer.
    sonnet::renderer::frontend::Renderer renderer{backend};

    // Upload assets.
    const auto meshHandle = renderer.createMesh(buildCube());
    const auto shaderHandle = renderer.createShader(VERT_SRC, FRAG_SRC);

    const auto matHandle = renderer.createMaterial(sonnet::api::render::MaterialTemplate{
        .shaderHandle = shaderHandle,
        .renderState  = {},
    });

    FlyCamera camera;
    float rotation = 0.0f;

    double prevTime = glfwGetTime();

    while (!window.shouldClose()) {
        // Delta time.
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();

        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape)) {
            window.requestClose();
        }

        const auto fbSize = window.getFrameBufferSize();
        camera.update(dt, input, fbSize);

        rotation += 45.0f * dt;

        // Clear.
        backend.bindDefaultRenderTarget();
        backend.setViewport(fbSize.x, fbSize.y);
        backend.clear({
            .colors  = {{0, {0.1f, 0.1f, 0.15f, 1.0f}}},
            .depth   = 1.0f,
        });

        // Build model matrix.
        glm::mat4 model{1.0f};
        model = glm::rotate(model, glm::radians(rotation),       {0, 1, 0});
        model = glm::rotate(model, glm::radians(rotation * 0.3f), {1, 0, 0});

        // Render.
        sonnet::api::render::FrameContext ctx{
            .viewMatrix       = camera.view(),
            .projectionMatrix = camera.proj(),
            .viewPosition     = camera.pos(),
            .viewportWidth    = fbSize.x,
            .viewportHeight   = fbSize.y,
            .deltaTime        = dt,
        };

        std::vector<sonnet::api::render::RenderItem> queue;
        queue.push_back({
            .mesh        = meshHandle,
            .material    = sonnet::api::render::MaterialInstance{matHandle},
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
