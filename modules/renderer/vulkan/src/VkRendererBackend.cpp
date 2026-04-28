#include <sonnet/renderer/vulkan/VkRendererBackend.h>
#include <sonnet/renderer/vulkan/VkGpuBuffer.h>
#include <sonnet/renderer/vulkan/VkGpuMeshFactory.h>
#include <sonnet/renderer/vulkan/VkRenderTarget.h>
#include <sonnet/renderer/vulkan/VkRenderTargetFactory.h>
#include <sonnet/renderer/vulkan/VkShader.h>
#include <sonnet/renderer/vulkan/VkShaderCompiler.h>
#include <sonnet/renderer/vulkan/VkTextureFactory.h>
#include <sonnet/renderer/vulkan/VkVertexInputState.h>

#include <algorithm>
#include <cstring>

#include <sonnet/window/GLFWWindow.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "VkBindState.h"
#include "VkCommandContext.h"
#include "VkDescriptorManager.h"
#include "VkDevice.h"
#include "VkInstance.h"
#include "VkPipelineCache.h"
#include "VkSamplerCache.h"
#include "VkSwapchain.h"
#include "VkUniformRing.h"
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

    // Destroy command pools first: that frees every recorded reference to
    // pipelines, descriptors, buffers, and samplers. After this, all the
    // resource teardowns below run with no live cmd-buffer bindings.
    m_commandContext.reset();

    if (m_imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->logical(), m_imguiPool, nullptr);
        m_imguiPool = VK_NULL_HANDLE;
    }

    m_uniformRing.reset();
    m_descriptorManager.reset();
    m_pipelineCache.reset();
    m_gpuMeshFactory.reset();
    m_renderTargetFactory.reset();
    m_textureFactory.reset();
    m_samplerCache.reset();
    m_shaderCompiler.reset();

    m_swapchain.reset();
    m_bindState.reset();

    if (m_surface != VK_NULL_HANDLE && m_instance) {
        vkDestroySurfaceKHR(m_instance->handle(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    m_device.reset();
    m_instance.reset();
}

void VkRendererBackend::initialize() {
    const auto &win = glfwWindow(m_window);

    // 1. Instance (w/ portability on macOS, debug-utils in debug builds).
    // Debug builds force validation on; Release builds respect the option so
    // tests/CI can enable it explicitly via BackendCreateOptions.
#if !defined(NDEBUG)
    const bool enableValidation = true;
#else
    const bool enableValidation = m_opts.enableValidation;
#endif
    m_instance = std::make_unique<Instance>(win.requiredVulkanInstanceExtensions(),
                                            enableValidation);

    // 2. Surface from GLFW.
    m_surface = win.createVulkanSurface(m_instance->handle());

    // 3. Device + VMA.
    m_device = std::make_unique<Device>(*m_instance, m_surface);

    // 4. Swapchain (default RT render pass + framebuffers + depth).
    const auto fb = m_window.getFrameBufferSize();
    m_lastFbSize = fb;
    m_swapchain = std::make_unique<Swapchain>(*m_device, m_surface, fb.x, fb.y);

    // 5. Per-frame command context + sync.
    m_commandContext = std::make_unique<CommandContext>(*m_device);

    // 6. Shared per-frame binding state consumed by VkGpuBuffer.
    m_bindState = std::make_unique<BindState>();

    // 7. Factories + pipeline cache + descriptor manager.
    m_shaderCompiler      = std::make_unique<VkShaderCompiler>(*m_device, *m_bindState);
    m_samplerCache        = std::make_unique<SamplerCache>(*m_device);
    m_textureFactory      = std::make_unique<VkTextureFactory>(*m_device, *m_samplerCache, *m_bindState);
    m_renderTargetFactory = std::make_unique<VkRenderTargetFactory>(*m_device, *m_samplerCache, *m_bindState);
    m_gpuMeshFactory      = std::make_unique<VkGpuMeshFactory>(*this);
    m_pipelineCache       = std::make_unique<PipelineCache>(*m_device);
    m_descriptorManager   = std::make_unique<DescriptorManager>(*m_device, *m_samplerCache, *m_bindState);
    // 2 MiB per frame is comfortable for per-draw UBOs in a demo-scale scene;
    // UniformRing aligns to minUniformBufferOffsetAlignment internally.
    m_uniformRing         = std::make_unique<UniformRing>(*m_device, 2u * 1024u * 1024u);

    // 8. Dedicated descriptor pool for ImGui's textures. imgui_impl_vulkan
    // (docking branch) also allocates separate SAMPLER and SAMPLED_IMAGE
    // sets via ImGui_ImplVulkan_AddTexture, so the pool needs capacity for
    // all three types.
    const std::array<VkDescriptorPoolSize, 3> imguiPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER,                1024},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1024},
    };
    VkDescriptorPoolCreateInfo imguiPoolInfo{};
    imguiPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    imguiPoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    imguiPoolInfo.maxSets       = 1024;
    imguiPoolInfo.poolSizeCount = static_cast<std::uint32_t>(imguiPoolSizes.size());
    imguiPoolInfo.pPoolSizes    = imguiPoolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(m_device->logical(), &imguiPoolInfo, nullptr, &m_imguiPool));

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

    // Reset this frame's descriptor pool + uniform ring before any draw
    // allocates from them.
    m_descriptorManager->beginFrame(m_commandContext->currentFrameIndex());
    m_uniformRing->beginFrame(m_commandContext->currentFrameIndex());

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

    // Reset frame-scoped binding state and queue the swapchain's default pass
    // as pending. Nothing is recorded until the first drawIndexed (or the
    // user binds another RT). This is the deferred-pass model from plan §6.
    m_bindState->reset();
    // Expose the per-frame command buffer to BindState so VkGpuBuffer::update
    // can route UBO writes through vkCmdUpdateBuffer (serialized with command
    // execution) instead of host memcpy. Set after reset() above so it isn't
    // wiped, and cleared in endFrame.
    m_bindState->currentCmd = frame.cmd;
    m_pending = {};
    m_pending.renderPass  = m_swapchain->defaultRenderPass();
    m_pending.framebuffer = m_swapchain->framebuffer(imageIx);
    m_pending.extent      = m_swapchain->extent();
    m_pending.colorCount  = 1;
    m_pending.hasDepth    = true;
    m_passActive = false;
    m_bindState->passActive = false;

    m_framePending = true;
}

void VkRendererBackend::endFrame() {
    if (!m_framePending) return; // e.g. minimized or acquire returned OUT_OF_DATE.

    auto &frame = m_commandContext->current();

    // If the user never bound the default RT before endFrame (or never drew
    // anywhere), fold to the swapchain pass so the swapchain image transitions
    // to PRESENT_SRC. ensurePassActive begins it; it'll be ended below.
    // m_framePending stays true until after the inline ensurePassActive so
    // that helper isn't tripped by its own "frame not pending" guard.
    if (m_passActive && m_pending.renderPass != m_swapchain->defaultRenderPass()) {
        // An offscreen pass is recording; end it and switch to the default RT.
        vkCmdEndRenderPass(frame.cmd);
        m_passActive = false;
    m_bindState->passActive = false;
        m_pending = {};
        m_pending.renderPass  = m_swapchain->defaultRenderPass();
        m_pending.framebuffer = m_swapchain->framebuffer(m_pendingImageIndex);
        m_pending.extent      = m_swapchain->extent();
        m_pending.colorCount  = 1;
        m_pending.hasDepth    = true;
    }
    if (!m_passActive) {
        // Default pass hasn't been begun yet — force a transition-only pass.
        ensurePassActive();
    }
    vkCmdEndRenderPass(frame.cmd);
    m_passActive   = false;
    m_framePending = false;
    m_bindState->currentCmd = VK_NULL_HANDLE; // mirrors the begin-frame setup.
    VK_CHECK(vkEndCommandBuffer(frame.cmd));

    // renderFinished is per-swapchain-image (owned by Swapchain) so that with
    // N images and fewer frame slots, a semaphore is never re-signaled while
    // its previous present is still in flight.
    const VkSemaphore presentReady = m_swapchain->renderFinished(m_pendingImageIndex);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &frame.imageAvailable;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &frame.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &presentReady;
    VK_CHECK(vkQueueSubmit(m_device->graphicsQueue(), 1, &submit, frame.inFlight));

    const VkResult pres = m_swapchain->present(m_pendingImageIndex, presentReady);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else if (pres != VK_SUCCESS) {
        throw VulkanError(std::string("vkQueuePresentKHR failed: ") + vkResultToString(pres));
    }

    m_commandContext->advance();
}

// ── Framebuffer / pass deferral ────────────────────────────────────────────────

void VkRendererBackend::clear(const api::render::ClearOptions &options) {
    // Fold into pending clear values; the next pass-begin merges them into
    // VkRenderPassBeginInfo.pClearValues. If a pass is already recording (i.e.
    // a draw has happened), Vulkan's render pass loadOp can't be retargeted —
    // a future iteration could emit vkCmdClearAttachments here, but the engine
    // never calls clear() mid-pass in practice (see plan §6).
    for (const auto &c : options.colors) {
        if (c.attachmentIndex >= kMaxColorAttachments) continue;
        VkClearColorValue v{};
        v.float32[0] = c.value.r;
        v.float32[1] = c.value.g;
        v.float32[2] = c.value.b;
        v.float32[3] = c.value.a;
        m_pending.colorClears[c.attachmentIndex] = v;
    }
    if (options.depth) {
        m_pending.depthClearSet = true;
        m_pending.depthClear    = *options.depth;
    }
}

void VkRendererBackend::bindDefaultRenderTarget() {
    if (!m_framePending) return;
    if (m_passActive) {
        vkCmdEndRenderPass(m_commandContext->current().cmd);
        m_passActive = false;
    m_bindState->passActive = false;
    }
    m_pending = {};
    m_pending.renderPass  = m_swapchain->defaultRenderPass();
    m_pending.framebuffer = m_swapchain->framebuffer(m_pendingImageIndex);
    m_pending.extent      = m_swapchain->extent();
    m_pending.colorCount  = 1;
    m_pending.hasDepth    = true;
}

void VkRendererBackend::bindRenderTarget(const api::render::IRenderTarget &target) {
    if (!m_framePending) return;

    // Only VkRenderTarget is supported under this backend (the frontend's
    // BorrowedTexture wraps a texture, not a render target).
    const auto *vkRT = dynamic_cast<const VkRenderTarget *>(&target);
    if (!vkRT) {
        throw VulkanError("VkRendererBackend::bindRenderTarget: target is not a VkRenderTarget");
    }

    if (m_passActive) {
        vkCmdEndRenderPass(m_commandContext->current().cmd);
        m_passActive = false;
    m_bindState->passActive = false;
    }
    m_pending = {};
    m_pending.renderPass  = vkRT->renderPass();
    m_pending.framebuffer = vkRT->framebuffer();
    m_pending.extent      = {vkRT->width(), vkRT->height()};
    m_pending.colorCount  = vkRT->colorCount();
    m_pending.hasDepth    = vkRT->hasDepth();
}

void VkRendererBackend::ensurePassActive() {
    if (m_passActive || !m_framePending) return;
    if (m_pending.renderPass == VK_NULL_HANDLE) return; // beginFrame failed (minimized).

    auto &frame = m_commandContext->current();

    // Build clear-values array: one per color attachment + one for depth.
    // Defaults to opaque-black + 1.0 depth if the user didn't queue a clear.
    std::array<VkClearValue, kMaxColorAttachments + 1> clears{};
    for (std::uint32_t i = 0; i < m_pending.colorCount; ++i) {
        clears[i].color = m_pending.colorClears[i];
    }
    if (m_pending.hasDepth) {
        clears[m_pending.colorCount].depthStencil = {m_pending.depthClear, 0};
    }

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = m_pending.renderPass;
    rp.framebuffer       = m_pending.framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = m_pending.extent;
    rp.clearValueCount   = m_pending.colorCount + (m_pending.hasDepth ? 1u : 0u);
    rp.pClearValues      = clears.data();
    vkCmdBeginRenderPass(frame.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Default viewport + scissor to the render pass's full extent. Pipelines
    // declare both as dynamic state so we must call setViewport/setScissor
    // at least once per command buffer; doing it here means a caller that
    // skips backend.setViewport (because they're rendering "the whole RT")
    // doesn't trip validation. backend.setViewport overrides this default.
    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(m_pending.extent.width);
    vp.height   = static_cast<float>(m_pending.extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(frame.cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_pending.extent;
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);

    m_passActive            = true;
    m_bindState->passActive = true;
    m_activeRenderPass = m_pending.renderPass;
    m_activeColorCount = m_pending.colorCount;
    m_activeHasDepth   = m_pending.hasDepth;
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

void VkRendererBackend::setUniform(UniformLocation location, const core::UniformValue &value) {
    const auto *shader = m_bindState->currentShader;
    if (!shader) return;
    const auto *entry = shader->uniformEntry(static_cast<int>(location));
    if (!entry) return;

    auto writeBytes = [&](std::uint8_t *dst, std::uint32_t size) {
        std::visit([&](auto &&v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, core::Sampler>) {
                // Samplers don't live in UBO/push ranges — ignore.
            } else {
                const std::size_t n = std::min<std::size_t>(size, sizeof(T));
                std::memcpy(dst, &v, n);
            }
        }, value);
    };

    switch (entry->kind) {
        case ShaderUniformKind::PushConstant: {
            if (entry->offset + entry->size > BindState::kPushConstantBytes) return;
            writeBytes(m_bindState->pushConstantStaging.data() + entry->offset, entry->size);
            const auto end = entry->offset + entry->size;
            if (end > m_bindState->pushConstantDirtyEnd) {
                m_bindState->pushConstantDirtyEnd = end;
            }
            return;
        }
        case ShaderUniformKind::PerDrawUbo: {
            if (entry->offset + entry->size > BindState::kPerDrawStagingBytes) return;
            writeBytes(m_bindState->perDrawStaging.data() + entry->offset, entry->size);
            const auto end = entry->offset + entry->size;
            if (end > m_bindState->perDrawDirtyEnd) {
                m_bindState->perDrawDirtyEnd = end;
            }
            return;
        }
        case ShaderUniformKind::MaterialSampler: {
            // texture->bind(slot) staged the texture in texturesBySlot[slot];
            // route it to the descriptor binding the shader expects. For
            // sampler arrays, arrayElement disambiguates uShadowMaps[i] across
            // entries that share the same binding number.
            std::uint32_t slot = 0;
            std::visit([&](auto &&v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, core::Sampler>) {
                    slot = static_cast<std::uint32_t>(v);
                }
            }, value);
            if (slot >= BindState::kMaxMaterialTextures) return;
            const auto dstIx = entry->binding + entry->arrayElement;
            if (dstIx >= BindState::kMaxMaterialTextures) return;
            m_bindState->materialTextures[dstIx] = m_bindState->texturesBySlot[slot];
            return;
        }
        case ShaderUniformKind::Unknown:
            return;
    }
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

    // Begin the pending render pass on demand: state setters / clear / VIS
    // bind / shader bind all run "outside the pass" from the engine's POV;
    // we record vkCmdBeginRenderPass right before the first draw on this RT.
    ensurePassActive();

    auto &frame = m_commandContext->current();

    VkPipeline pipeline = m_pipelineCache->getOrCreate(
        *shader, m_renderState, *vis, m_activeRenderPass,
        m_activeColorCount, m_activeHasDepth);
    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind set 0 (frame-wide UBOs: Camera + Lights) if the shader declares it.
    if (VkDescriptorSet frameSet = m_descriptorManager->allocateFrameSet0(*shader);
        frameSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shader->pipelineLayout(), 0, 1, &frameSet, 0, nullptr);
    }

    // Bind set 1 (material textures) if the shader declares it.
    if (VkDescriptorSet matSet = m_descriptorManager->allocateMaterialSet1(*shader);
        matSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shader->pipelineLayout(), 1, 1, &matSet, 0, nullptr);
    }

    // Bind set 2 (PerDraw UBO) if the shader declares one: copy staged bytes
    // into the frame ring, allocate a descriptor set pointing to that slice,
    // and bind it.
    if (const auto perDrawSize = shader->reflection().perDrawUboSize;
        perDrawSize > 0) {
        auto ring = m_uniformRing->allocate(perDrawSize);
        std::memcpy(ring.mapped, m_bindState->perDrawStaging.data(), perDrawSize);
        if (VkDescriptorSet pdSet = m_descriptorManager->allocatePerDrawSet2(
                *shader, m_uniformRing->buffer(), ring.offset, perDrawSize);
            pdSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    shader->pipelineLayout(), 2, 1, &pdSet, 0, nullptr);
        }
    }

    // Flush push constants for any uniform writes that happened since the
    // last draw. Vulkan validation rejects a single vkCmdPushConstants whose
    // (offset, size, stageFlags) tuple isn't fully covered by one or more
    // matching ranges in the pipeline layout, so emit one call per
    // declared range — each carries that range's exact (offset, size,
    // stageFlags). Skip ranges entirely past the dirty hwm so a draw that
    // only wrote vertex-stage push data doesn't emit a stale fragment-stage
    // push.
    if (m_bindState->pushConstantDirtyEnd > 0) {
        for (const auto &r : shader->reflection().pushConstantRanges) {
            if (r.offset >= m_bindState->pushConstantDirtyEnd) continue;
            const std::uint32_t end =
                std::min(r.offset + r.size, m_bindState->pushConstantDirtyEnd);
            const std::uint32_t writeSize = end - r.offset;
            vkCmdPushConstants(frame.cmd, shader->pipelineLayout(), r.stageFlags,
                                r.offset, writeSize,
                                m_bindState->pushConstantStaging.data() + r.offset);
        }
    }

    const VkBuffer     vbo       = m_bindState->currentVertex;
    const VkDeviceSize vboOffset = 0;
    vkCmdBindVertexBuffers(frame.cmd, 0, 1, &vbo, &vboOffset);
    vkCmdBindIndexBuffer(frame.cmd, m_bindState->currentIndex, 0, m_bindState->indexType);

    vkCmdDrawIndexed(frame.cmd, static_cast<std::uint32_t>(indexCount), 1, 0, 0, 0);

    // Reset per-draw scoped state (material textures, push-constant hwm) so
    // the next material bind starts clean.
    m_bindState->clearDrawScopedState();
}

// ── Factory accessors ──────────────────────────────────────────────────────────

api::render::IShaderCompiler     &VkRendererBackend::shaderCompiler()     { return *m_shaderCompiler; }
api::render::ITextureFactory     &VkRendererBackend::textureFactory()     { return *m_textureFactory; }
api::render::IRenderTargetFactory &VkRendererBackend::renderTargetFactory() { return *m_renderTargetFactory; }
api::render::IGpuMeshFactory     &VkRendererBackend::gpuMeshFactory()     { return *m_gpuMeshFactory; }

// ── ImGui integration ──────────────────────────────────────────────────────────

VkRendererBackend::ImGuiInitInfo VkRendererBackend::imGuiInitInfo() const {
    return ImGuiInitInfo{
        .instance       = m_instance->handle(),
        .physicalDevice = m_device->physical(),
        .device         = m_device->logical(),
        .queueFamily    = m_device->graphicsFamily(),
        .queue          = m_device->graphicsQueue(),
        .renderPass     = m_swapchain->defaultRenderPass(),
        .minImageCount  = m_swapchain->imageCount(),
        .imageCount     = m_swapchain->imageCount(),
        .descriptorPool = m_imguiPool,
    };
}

void VkRendererBackend::renderImGui() {
    if (!m_framePending) return;
    auto *draw = ImGui::GetDrawData();
    if (!draw || draw->CmdListsCount == 0) return;
    // imgui_impl_vulkan must record inside an active render pass — use the
    // pending one (typically the default-RT pass set up by the demo before
    // the ImGui block).
    ensurePassActive();
    ImGui_ImplVulkan_RenderDrawData(draw, m_commandContext->current().cmd);
}

void VkRendererBackend::prepareForShutdown() {
    if (m_device) m_device->waitIdle();
    // Destroying the command context releases every recorded reference to
    // descriptors, pipelines, buffers and samplers. After this returns,
    // application-owned resources can destruct in any order with no
    // validation noise.
    m_commandContext.reset();
    m_initialized = false;
}

// ── Private ────────────────────────────────────────────────────────────────────

void VkRendererBackend::recreateSwapchain() {
    const auto fb = m_window.getFrameBufferSize();
    if (fb.x == 0 || fb.y == 0) return; // wait for non-zero size
    m_device->waitIdle();
    m_swapchain->recreate(fb.x, fb.y);
    spdlog::debug("[vulkan] swapchain recreated ({}x{})", fb.x, fb.y);
}

} // namespace sonnet::renderer::vulkan
