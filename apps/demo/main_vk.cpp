// Sonnet v2 — Vulkan demo (Phases 1–4)
//
// Exercises the full Vulkan draw path plus ImGui overlay:
//   instance → device → swapchain → command context → buffer → shader
//   (glslang) → pipeline cache → descriptor manager → drawIndexed
//   → imgui_impl_vulkan overlay → present.

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/api/render/VertexLayout.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/renderer/frontend/BackendFactory.h>
#include <sonnet/renderer/vulkan/VkRendererBackend.h>
#include <sonnet/ui/ImGuiLayer.h>
#include <sonnet/window/GLFWInputAdapter.h>
#include <sonnet/window/GLFWWindow.h>

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

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

// Exercises the set=2 PerDraw UBO path: a tint applied per draw.
// Routed through VkRendererBackend::setUniform → VkUniformRing →
// per-draw descriptor set.
layout(std140, set = 2, binding = 0) uniform PerDraw {
    vec4 uTint;
} pd;

void main() {
    fragColor = vColor * pd.uTint;
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

    auto  backendPtr = sonnet::renderer::frontend::makeBackend(window);
    auto *backend    = static_cast<sonnet::renderer::vulkan::VkRendererBackend *>(backendPtr.get());
    backend->initialize();

    // ── ImGui on Vulkan ───────────────────────────────────────────────────────
    sonnet::ui::ImGuiLayer imgui;
    const auto info = backend->imGuiInitInfo();
    imgui.init(window.handle(), sonnet::ui::VulkanInitInfo{
        .instance       = info.instance,
        .physicalDevice = info.physicalDevice,
        .device         = info.device,
        .queueFamily    = info.queueFamily,
        .queue          = info.queue,
        .renderPass     = info.renderPass,
        .minImageCount  = info.minImageCount,
        .imageCount     = info.imageCount,
        .descriptorPool = info.descriptorPool,
    });

    // ── Triangle geometry + shader ────────────────────────────────────────────
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

        // 3D pass — triangle.
        shader->bind();
        vis->bind();
        vbo->bind();
        ibo->bind();
        const auto &uniforms = shader->getUniforms();
        if (const auto it = uniforms.find("uModel"); it != uniforms.end()) {
            backend->setUniform(it->second.location, sonnet::core::UniformValue{model});
        }
        // PerDraw UBO: gentle bluish tint that breathes with the rotation.
        const float t = 0.5f + 0.5f * std::sin(glm::radians(rot));
        const glm::vec4 tint{1.0f - 0.25f * t, 1.0f - 0.25f * t, 1.0f + 0.15f * t, 1.0f};
        if (const auto it = uniforms.find("uTint"); it != uniforms.end()) {
            backend->setUniform(it->second.location, sonnet::core::UniformValue{tint});
        }
        backend->drawIndexed(3);

        // ImGui pass — same render pass, layered on top.
        imgui.begin();
        ImGui::Begin("Sonnet v2 (Vulkan)");
        ImGui::Text("FPS: %.1f", dt > 0.0f ? 1.0f / dt : 0.0f);
        ImGui::Text("Press Escape to exit.");
        ImGui::End();
        imgui.end();

        backend->renderImGui();
        backend->endFrame();

        window.swapBuffers();
        input.nextFrame();
    }

    // prepareForShutdown destroys the backend's per-frame command pools, which
    // releases every recorded reference to ImGui's per-frame vertex/index
    // buffers, font sampler and pipelines. It must run *before*
    // imgui.shutdown(), which is what actually destroys those ImGui resources.
    backend->prepareForShutdown();
    imgui.shutdown();
    return 0;
}
