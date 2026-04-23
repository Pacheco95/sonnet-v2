#include <sonnet/renderer/vulkan/VkRendererBackend.h>

#include <sonnet/window/GLFWWindow.h>

#include "VkCommandContext.h"
#include "VkDevice.h"
#include "VkInstance.h"
#include "VkStubs.h"
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
    // Destruction order matters: swapchain/cmdctx before device; device before instance.
    m_commandContext.reset();
    m_swapchain.reset();
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

    // 6. Phase-1 factory stubs. Replaced in Phase 2.
    m_shaderCompiler      = std::make_unique<ShaderCompiler>();
    m_textureFactory      = std::make_unique<TextureFactory>();
    m_renderTargetFactory = std::make_unique<RenderTargetFactory>();
    m_gpuMeshFactory      = std::make_unique<GpuMeshFactory>();

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

void VkRendererBackend::setFillMode(api::render::FillMode) {}
void VkRendererBackend::setDepthTest(bool) {}
void VkRendererBackend::setDepthWrite(bool) {}
void VkRendererBackend::setDepthFunc(api::render::DepthFunction) {}
void VkRendererBackend::setCull(api::render::CullMode) {}
void VkRendererBackend::setBlend(bool) {}
void VkRendererBackend::setBlendFunc(api::render::BlendFactor, api::render::BlendFactor) {}
void VkRendererBackend::setSRGB(bool) {}

// ── Resource creation (Phase 2) ────────────────────────────────────────────────

std::unique_ptr<api::render::IGpuBuffer> VkRendererBackend::createBuffer(
    api::render::BufferType, const void *, std::size_t) {
    SN_VK_TODO("createBuffer — Phase 2");
}

std::unique_ptr<api::render::IVertexInputState> VkRendererBackend::createVertexInputState(
    const api::render::VertexLayout &,
    const api::render::IGpuBuffer   &,
    const api::render::IGpuBuffer   &) {
    SN_VK_TODO("createVertexInputState — Phase 2");
}

// ── Uniforms & drawing (Phase 3) ───────────────────────────────────────────────

void VkRendererBackend::setUniform(UniformLocation, const core::UniformValue &) {
    SN_VK_TODO("setUniform — Phase 3");
}

void VkRendererBackend::drawIndexed(std::size_t) {
    SN_VK_TODO("drawIndexed — Phase 3");
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
