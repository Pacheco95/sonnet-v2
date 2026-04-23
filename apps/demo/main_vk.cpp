// Sonnet v2 — Vulkan demo (Phases 1–3)
//
// Minimal triangle renderer that exercises the full Vulkan draw path:
//   instance → device → swapchain → command context → buffer → shader
//   (glslang) → pipeline cache → descriptor manager → drawIndexed.
// The shader uses a push constant for the model matrix, which maps onto
// the push-constant staging path in VkRendererBackend::setUniform. No UBOs
// and no material textures are needed, so set=0 and set=1 are both unused.

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/api/render/VertexLayout.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/renderer/frontend/BackendFactory.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>

namespace {

constexpr const char *kVertSrc = R"glsl(
#version 460

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(push_constant) uniform Push {
    mat4 uModel;
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = pc.uModel * vec4(aPosition, 1.0);
    vColor      = aColor;
}
)glsl";

constexpr const char *kFragSrc = R"glsl(
#version 460

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)glsl";

struct Vertex {
    float pos[3];
    float color[4];
};

} // namespace

int main() {
    sonnet::window::GLFWWindow       window{{1280, 720, "Sonnet v2 Demo (Vulkan)"}};
    sonnet::input::InputSystem       input;
    sonnet::window::GLFWInputAdapter adapter{input};
    window.setInputAdapter(&adapter);

    auto backend = sonnet::renderer::frontend::makeBackend(window);
    backend->initialize();

    // ── Geometry: single triangle, CCW-wound so VK_FRONT_FACE_COUNTER_CLOCKWISE
    // plus VK_CULL_MODE_BACK_BIT keeps it visible. ────────────────────────────
    const std::array<Vertex, 3> verts{{
        {{ 0.0f,  0.6f, 0.0f}, {1.0f, 0.3f, 0.3f, 1.0f}},
        {{-0.6f, -0.5f, 0.0f}, {0.3f, 1.0f, 0.3f, 1.0f}},
        {{ 0.6f, -0.5f, 0.0f}, {0.3f, 0.3f, 1.0f, 1.0f}},
    }};
    const std::array<std::uint32_t, 3> indices{0u, 1u, 2u};

    auto vbo = backend->createBuffer(
        sonnet::api::render::BufferType::Vertex, verts.data(),  sizeof(verts));
    auto ibo = backend->createBuffer(
        sonnet::api::render::BufferType::Index,  indices.data(), sizeof(indices));

    // VertexLayout derived from typed KnownAttributeSet — matches shader's
    // location 0 (vec3 position) + location 1 (vec4 color).
    sonnet::api::render::KnownAttributeSet attrs;
    attrs.insert(sonnet::api::render::PositionAttribute{});
    attrs.insert(sonnet::api::render::ColorAttribute{});
    sonnet::api::render::VertexLayout layout{std::move(attrs)};

    auto vis    = backend->createVertexInputState(layout, *vbo, *ibo);
    auto shader = backend->shaderCompiler()(kVertSrc, kFragSrc);

    // ── Frame loop ────────────────────────────────────────────────────────────
    double prevTime = glfwGetTime();
    float  rot      = 0.0f;

    while (!window.shouldClose()) {
        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - prevTime);
        prevTime = now;

        window.pollEvents();
        if (input.isKeyJustPressed(sonnet::api::input::Key::Escape)) window.requestClose();

        rot += dt * 60.0f;
        const auto model = glm::rotate(glm::mat4(1.0f), glm::radians(rot),
                                        glm::vec3(0.0f, 0.0f, 1.0f));

        const auto fb = window.getFrameBufferSize();
        backend->beginFrame();
        backend->setViewport(fb.x, fb.y);
        backend->setDepthTest(false);
        backend->setCull(sonnet::api::render::CullMode::None);

        shader->bind();
        vis->bind();
        vbo->bind();
        ibo->bind();

        if (const auto it = shader->getUniforms().find("uModel");
            it != shader->getUniforms().end()) {
            backend->setUniform(it->second.location, sonnet::core::UniformValue{model});
        }

        backend->drawIndexed(3);
        backend->endFrame();

        window.swapBuffers();
        input.nextFrame();
    }

    return 0;
}
