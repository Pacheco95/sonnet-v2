#include <sonnet/renderer/vulkan/VkRendererBackend.h>
#include <sonnet/renderer/vulkan/VkGpuBuffer.h>
#include <sonnet/renderer/vulkan/VkGpuMeshFactory.h>
#include <sonnet/renderer/vulkan/VkRenderTargetFactory.h>
#include <sonnet/renderer/vulkan/VkShader.h>
#include <sonnet/renderer/vulkan/VkShaderCompiler.h>
#include <sonnet/renderer/vulkan/VkTextureFactory.h>
#include <sonnet/renderer/vulkan/VkVertexInputState.h>

#include <sonnet/window/GLFWWindow.h>

#include "VkBindState.h"
#include "VkCommandContext.h"
#include "VkDescriptorManager.h"
#include "VkDevice.h"
#include "VkInstance.h"
#include "VkPipelineCache.h"
#include "VkSamplerCache.h"
#include "VkSwapchain.h"
#include "VkUtils.h"

#include <array>

namespace sonnet::renderer::vulkan {

namespace {

// Cast an IWindow& down to the concrete GLFWWindow. The Vulkan backend
// depends on GLFW for surface creation; no other IWindow impl is supported.
const sonnet::window::GLFWWindow &glfwWindow(const api::window::IWindow &w) {
    const auto *g = dynamic_cast<const sonnet::window::GLFWWindow *>(&w);
    if (!g) {
        throw VulkanError("VkRendererBackend requires a GLFWWindow");
    }
    return *g;
}

} // namespace

VkRendererBackend::VkRendererBackend(api::window::IWindow &window,
                                     const api::render::BackendCreateOptions &opts)
    : m_window(window), m_opts(opts) {}

VkRendererBackend::~VkRendererBackend() {
    if (m_device) m_device->waitIdle();
    // Destruction order matters: resources → pipelines/descriptors → device → instance.
    m_descriptorManager.reset();
    m_pipelineCache.reset();
    m_gpuMeshFactory.reset();
    m_renderTargetFactory.reset();
    m_textureFactory.reset();
    m_samplerCache.reset();
    m_shaderCompiler.reset();
    m_commandContext.reset();
    m_swapchain.reset();
    m_bindState.reset();
    m_device.reset();
    m_instance.reset();
}

void VkRendererBackend::initialize() {
    const auto &win = glfwWindow(m_window);

    // 1. Instance (w/ portability on macOS, debug-utils in debug builds).
    const bool enableValidation =
#if !defined(NDEBUG)
        true || m_opts.enableValidation;
#else
        m_opts.enableValidation;
#endif
    m_instance = std::make_unique<Instance>(win.requiredVulkanInstanceExtensions(),
                                            enableValidation);

    // 2. Surface from GLFW.
    VkSurfaceKHR surface = win.createVulkanSurface(m_instance->handle());

    // 3. Device + VMA.
    m_device = std::make_unique<Device>(*m_instance, surface);

    // 4. Swapchain (default RT render pass + framebuffers + depth).
    const auto fb = m_window.getFrameBufferSize();
    m_lastFbSize = fb;
    m_swapchain = std::make_unique<Swapchain>(*m_device, surface, fb.x, fb.y);

    // 5. Per-frame command context + sync.
    m_commandContext = std::make_unique<CommandContext>(*m_device);

    // 6. Shared per-frame binding state consumed by VkGpuBuffer.
    m_bindState = std::make_unique<BindState>();

    // 7. Factories + pipeline cache + descriptor manager.
    m_shaderCompiler      = std::make_unique<VkShaderCompiler>(*m_device, *m_bindState);
    m_samplerCache        = std::make_unique<SamplerCache>(*m_device);
    m_textureFactory      = std::make_unique<VkTextureFactory>(*m_device, *m_samplerCache);
    m_renderTargetFactory = std::make_unique<VkRenderTargetFactory>(*m_device, *m_samplerCache);
    m_gpuMeshFactory      = std::make_unique<VkGpuMeshFactory>(*this);
    m_pipelineCache       = std::make_unique<PipelineCache>(*m_device);
    m_descriptorManager   = std::make_unique<DescriptorManager>(*m_device, *m_bindState);

    m_initialized = true;
    spdlog::info("[vulkan] VkRendererBackend initialized");
}

// ── Frame lifecycle ────────────────────────────────────────────────────────────

void VkRendererBackend::beginFrame() {
    if (!m_initialized) throw VulkanError("beginFrame before initialize");

    // If a resize was requested between frames, or the fb actually changed,
    // recreate the swapchain before acquiring.
    const auto fb = m_window.getFrameBufferSize();
    if (fb.x == 0 || fb.y == 0) {
        // Minimized; skip this frame. Caller still needs endFrame as a no-op.
        m_framePending = false;
        return;
    }
    if (fb != m_lastFbSize || m_resizeRequested) {
        m_lastFbSize = fb;
        m_resizeRequested = false;
        recreateSwapchain();
    }

    auto &frame = m_commandContext->beginFrame();

    // Reset this frame's descriptor pool before any draw allocates from it.
    m_descriptorManager->beginFrame(m_commandContext->currentFrameIndex());

    std::uint32_t imageIx = 0;
    const VkResult acq = m_swapchain->acquireNextImage(frame.imageAvailable, imageIx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        m_resizeRequested = true;
        m_framePending    = false;
        return;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        throw VulkanError(std::string("vkAcquireNextImageKHR failed: ") + vkResultToString(acq));
    }

    // Store the acquired image index in a local the end-of-frame path can read.
    // (Keep it as state on `this` for simplicity in Phase 1.)
    m_pendingImageIndex = imageIx;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.cmd, &beginInfo));

    // Begin the default render pass immediately with a fixed dark-gray clear.
    // Phase 3 replaces this with the deferred-pass model (§6 of the plan).
    const VkClearValue clears[2] = {
        {.color        = {{0.08f, 0.08f, 0.12f, 1.0f}}},
        {.depthStencil = {1.0f, 0}},
    };
    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = m_swapchain->defaultRenderPass();
    rp.framebuffer       = m_swapchain->framebuffer(imageIx);
    rp.renderArea.extent = m_swapchain->extent();
    rp.clearValueCount   = 2;
    rp.pClearValues      = clears;
    vkCmdBeginRenderPass(frame.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Reset frame-scoped binding state and record the active pass so that
    // drawIndexed can key pipelines correctly.
    m_bindState->reset();
    m_activeRenderPass = m_swapchain->defaultRenderPass();
    m_activeColorCount = 1;
    m_activeHasDepth   = true;

    m_framePending = true;
}

void VkRendererBackend::endFrame() {
    if (!m_framePending) return; // e.g. minimized or acquire returned OUT_OF_DATE.
    m_framePending = false;

    auto &frame = m_commandContext->current();

    vkCmdEndRenderPass(frame.cmd);
    VK_CHECK(vkEndCommandBuffer(frame.cmd));

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &frame.imageAvailable;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &frame.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &frame.renderFinished;
    VK_CHECK(vkQueueSubmit(m_device->graphicsQueue(), 1, &submit, frame.inFlight));

    const VkResult pres = m_swapchain->present(m_pendingImageIndex, frame.renderFinished);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else if (pres != VK_SUCCESS) {
        throw VulkanError(std::string("vkQueuePresentKHR failed: ") + vkResultToString(pres));
    }

    m_commandContext->advance();
}

// ── Framebuffer ────────────────────────────────────────────────────────────────

void VkRendererBackend::clear(const api::render::ClearOptions & /*options*/) {
    // Phase 1: clear is already applied by the render-pass begin in beginFrame().
    // Phase 3 replaces this with the deferred-pass + vkCmdClearAttachments model.
}

void VkRendererBackend::bindDefaultRenderTarget() {
    // Phase 1: always rendering to the default RT; no-op. Phase 3 wires up
    // multi-RT switching with render-pass deferral (plan §6).
}

void VkRendererBackend::bindRenderTarget(const api::render::IRenderTarget & /*target*/) {
    SN_VK_TODO("bindRenderTarget — Phase 2/3");
}

void VkRendererBackend::setViewport(std::uint32_t width, std::uint32_t height) {
    if (!m_framePending) return;
    auto &frame = m_commandContext->current();
    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(width);
    vp.height   = static_cast<float>(height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(frame.cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
}

// ── Pipeline state (Phase 3) ───────────────────────────────────────────────────

void VkRendererBackend::setFillMode(api::render::FillMode mode)        { m_renderState.fill = mode; }
void VkRendererBackend::setDepthTest(bool enabled)                      { m_renderState.depthTest = enabled; }
void VkRendererBackend::setDepthWrite(bool enabled)                     { m_renderState.depthWrite = enabled; }
void VkRendererBackend::setDepthFunc(api::render::DepthFunction func)   { m_renderState.depthFunc = func; }
void VkRendererBackend::setCull(api::render::CullMode mode)             { m_renderState.cull = mode; }
void VkRendererBackend::setBlend(bool enabled) {
    // Blend mode selection happens via setBlendFunc; enabled is redundant info
    // under Vulkan but may disable blending if paired with Opaque factors.
    if (!enabled) m_renderState.blend = api::render::BlendMode::Opaque;
}
void VkRendererBackend::setBlendFunc(api::render::BlendFactor src, api::render::BlendFactor dst) {
    // Map the (src, dst) pair back onto the BlendMode enum; Renderer uses a
    // fixed mapping (Alpha = SrcAlpha/OneMinusSrcAlpha, Additive = One/One).
    using F = api::render::BlendFactor;
    if      (src == F::SrcAlpha && dst == F::OneMinusSrcAlpha) m_renderState.blend = api::render::BlendMode::Alpha;
    else if (src == F::One      && dst == F::One)              m_renderState.blend = api::render::BlendMode::Additive;
    else                                                        m_renderState.blend = api::render::BlendMode::Opaque;
}
void VkRendererBackend::setSRGB(bool) {
    // Swapchain sRGB is baked into the image format; no-op per-draw.
}

// ── Resource creation (Phase 2) ────────────────────────────────────────────────

std::unique_ptr<api::render::IGpuBuffer> VkRendererBackend::createBuffer(
    api::render::BufferType type, const void *data, std::size_t size) {
    return std::make_unique<VkGpuBuffer>(*m_device, *m_bindState, type, data, size);
}

std::unique_ptr<api::render::IVertexInputState> VkRendererBackend::createVertexInputState(
    const api::render::VertexLayout &layout,
    const api::render::IGpuBuffer   &/*vertexBuffer*/,
    const api::render::IGpuBuffer   &/*indexBuffer*/) {
    // Vertex+index buffer references are unused in Vulkan's "VAO" — the actual
    // binding happens inside drawIndexed via vkCmdBindVertexBuffers /
    // vkCmdBindIndexBuffer (consumed from BindState that GpuMesh::bind writes).
    return std::make_unique<VkVertexInputState>(layout, *m_bindState);
}

// ── Uniforms & drawing (Phase 3) ───────────────────────────────────────────────

void VkRendererBackend::setUniform(UniformLocation, const core::UniformValue &) {
    SN_VK_TODO("setUniform — Phase 3");
}

void VkRendererBackend::drawIndexed(std::size_t indexCount) {
    if (!m_framePending) return;

    const VkShader           *shader = m_bindState->currentShader;
    const VkVertexInputState *vis    = m_bindState->currentVertexInput;
    if (!shader || !vis) {
        // Frontend hasn't bound a shader+VIS yet; skip gracefully.
        return;
    }
    if (m_bindState->currentVertex == VK_NULL_HANDLE ||
        m_bindState->currentIndex  == VK_NULL_HANDLE) {
        return;
    }

    auto &frame = m_commandContext->current();

    VkPipeline pipeline = m_pipelineCache->getOrCreate(
        *shader, m_renderState, *vis, m_activeRenderPass,
        m_activeColorCount, m_activeHasDepth);
    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind set 0 (frame-wide UBOs: Camera + Lights) if the shader declares one.
    VkDescriptorSet frameSet = m_descriptorManager->allocateFrameSet0(*shader);
    if (frameSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shader->pipelineLayout(), 0, 1, &frameSet, 0, nullptr);
    }

    const VkBuffer     vbo       = m_bindState->currentVertex;
    const VkDeviceSize vboOffset = 0;
    vkCmdBindVertexBuffers(frame.cmd, 0, 1, &vbo, &vboOffset);
    vkCmdBindIndexBuffer(frame.cmd, m_bindState->currentIndex, 0, m_bindState->indexType);

    vkCmdDrawIndexed(frame.cmd, static_cast<std::uint32_t>(indexCount), 1, 0, 0, 0);
}

// ── Factory accessors ──────────────────────────────────────────────────────────

api::render::IShaderCompiler     &VkRendererBackend::shaderCompiler()     { return *m_shaderCompiler; }
api::render::ITextureFactory     &VkRendererBackend::textureFactory()     { return *m_textureFactory; }
api::render::IRenderTargetFactory &VkRendererBackend::renderTargetFactory() { return *m_renderTargetFactory; }
api::render::IGpuMeshFactory     &VkRendererBackend::gpuMeshFactory()     { return *m_gpuMeshFactory; }

// ── Private ────────────────────────────────────────────────────────────────────

void VkRendererBackend::recreateSwapchain() {
    const auto fb = m_window.getFrameBufferSize();
    if (fb.x == 0 || fb.y == 0) return; // wait for non-zero size
    m_device->waitIdle();
    m_swapchain->recreate(fb.x, fb.y);
    spdlog::debug("[vulkan] swapchain recreated ({}x{})", fb.x, fb.y);
}

} // namespace sonnet::renderer::vulkan
