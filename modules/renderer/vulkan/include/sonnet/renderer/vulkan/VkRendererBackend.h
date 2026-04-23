#pragma once

#include <sonnet/api/render/BackendCreateOptions.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/api/render/RenderState.h>
#include <sonnet/api/window/IWindow.h>
#include <sonnet/core/Macros.h>

#include <vulkan/vulkan.h>

#include <memory>

namespace sonnet::renderer::vulkan {

class Instance;
class Device;
class Swapchain;
class CommandContext;
class VkTextureFactory;
class VkRenderTargetFactory;
class VkShaderCompiler;
class VkGpuMeshFactory;
class SamplerCache;
class PipelineCache;
class DescriptorManager;
struct BindState;

// VkRendererBackend — Vulkan implementation of IRendererBackend.
//
// Phase 1 scope: initialize/beginFrame/clear/setViewport/endFrame work end
// to end; every other method throws std::runtime_error with a TODO marker.
// Subsequent phases fill in the remaining methods incrementally.
class VkRendererBackend final : public api::render::IRendererBackend {
public:
    VkRendererBackend(api::window::IWindow &window,
                      const api::render::BackendCreateOptions &opts);
    ~VkRendererBackend() override;

    SN_NON_COPYABLE(VkRendererBackend);
    SN_NON_MOVABLE(VkRendererBackend);

    void initialize() override;

    // Frame lifecycle
    void beginFrame() override;
    void endFrame()   override;

    // Framebuffer
    void clear(const api::render::ClearOptions &options)                      override;
    void bindDefaultRenderTarget()                                             override;
    void bindRenderTarget(const api::render::IRenderTarget &target)            override;
    void setViewport(std::uint32_t width, std::uint32_t height)               override;

    // Pipeline state (Phase 3)
    void setFillMode(api::render::FillMode mode)                              override;
    void setDepthTest(bool enabled)                                           override;
    void setDepthWrite(bool enabled)                                          override;
    void setDepthFunc(api::render::DepthFunction func)                        override;
    void setCull(api::render::CullMode mode)                                  override;
    void setBlend(bool enabled)                                               override;
    void setBlendFunc(api::render::BlendFactor src, api::render::BlendFactor dst) override;
    void setSRGB(bool enabled)                                                override;

    // Resource creation (Phase 2)
    [[nodiscard]] std::unique_ptr<api::render::IGpuBuffer> createBuffer(
        api::render::BufferType type, const void *data, std::size_t size)     override;
    [[nodiscard]] std::unique_ptr<api::render::IVertexInputState> createVertexInputState(
        const api::render::VertexLayout &layout,
        const api::render::IGpuBuffer   &vertexBuffer,
        const api::render::IGpuBuffer   &indexBuffer)                         override;

    // Uniforms & drawing (Phase 3)
    void setUniform(UniformLocation location, const core::UniformValue &value) override;
    void drawIndexed(std::size_t indexCount)                                   override;

    // Factories
    [[nodiscard]] api::render::IShaderCompiler     &shaderCompiler()       override;
    [[nodiscard]] api::render::ITextureFactory     &textureFactory()       override;
    [[nodiscard]] api::render::IRenderTargetFactory &renderTargetFactory() override;
    [[nodiscard]] api::render::IGpuMeshFactory     &gpuMeshFactory()       override;

    [[nodiscard]] const core::RendererTraits &traits() const override { return core::presets::Vulkan; }

    // Vulkan-specific ImGui integration. The demo passes the info struct
    // to sonnet::ui::ImGuiLayer::init under SONNET_USE_VULKAN, and calls
    // renderImGui() after ImGui::Render each frame so the backend can
    // record imgui_impl_vulkan's draw commands into the current frame's
    // command buffer.
    struct ImGuiInitInfo {
        VkInstance       instance;
        VkPhysicalDevice physicalDevice;
        VkDevice         device;
        std::uint32_t    queueFamily;
        VkQueue          queue;
        VkRenderPass     renderPass;
        std::uint32_t    minImageCount;
        std::uint32_t    imageCount;
        VkDescriptorPool descriptorPool;
    };
    [[nodiscard]] ImGuiInitInfo imGuiInitInfo() const;
    void renderImGui();

private:
    api::window::IWindow                         &m_window;
    api::render::BackendCreateOptions             m_opts;

    std::unique_ptr<Instance>                     m_instance;
    std::unique_ptr<Device>                       m_device;
    std::unique_ptr<Swapchain>                    m_swapchain;
    std::unique_ptr<CommandContext>               m_commandContext;
    std::unique_ptr<BindState>                    m_bindState;

    std::unique_ptr<VkShaderCompiler>             m_shaderCompiler;
    std::unique_ptr<SamplerCache>                 m_samplerCache;
    std::unique_ptr<VkTextureFactory>             m_textureFactory;
    std::unique_ptr<VkRenderTargetFactory>        m_renderTargetFactory;
    std::unique_ptr<VkGpuMeshFactory>             m_gpuMeshFactory;
    std::unique_ptr<PipelineCache>                m_pipelineCache;
    std::unique_ptr<DescriptorManager>            m_descriptorManager;

    bool           m_initialized       = false;
    glm::uvec2     m_lastFbSize{0, 0};
    bool           m_framePending      = false; // set true between beginFrame/endFrame
    bool           m_resizeRequested   = false;
    std::uint32_t  m_pendingImageIndex = 0;

    // Live render state accumulated via setDepthTest/setBlend/etc. Consumed by
    // drawIndexed when it looks up the pipeline.
    api::render::RenderState m_renderState{};
    // Active pass for pipeline keying.
    VkRenderPass             m_activeRenderPass  = VK_NULL_HANDLE;
    std::uint32_t            m_activeColorCount  = 1;
    bool                     m_activeHasDepth    = true;

    // Dedicated descriptor pool for imgui_impl_vulkan. ImGui manages its own
    // allocations from this pool (one per texture it needs to sample).
    VkDescriptorPool         m_imguiPool         = VK_NULL_HANDLE;

    void recreateSwapchain();
};

} // namespace sonnet::renderer::vulkan
