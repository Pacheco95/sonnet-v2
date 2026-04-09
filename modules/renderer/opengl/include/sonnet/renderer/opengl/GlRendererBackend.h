#pragma once

#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/core/Macros.h>
#include <sonnet/renderer/opengl/GlGpuMeshFactory.h>
#include <sonnet/renderer/opengl/GlRenderTargetFactory.h>
#include <sonnet/renderer/opengl/GlShaderCompiler.h>
#include <sonnet/renderer/opengl/GlTextureFactory.h>

#include <memory>

namespace sonnet::renderer::opengl {

class GlRendererBackend final : public api::render::IRendererBackend {
public:
    GlRendererBackend();
    ~GlRendererBackend() override = default;

    SN_NON_COPYABLE(GlRendererBackend);
    SN_NON_MOVABLE(GlRendererBackend);

    void initialize() override;

    // Frame lifecycle
    void beginFrame() override;
    void endFrame()   override;

    // Framebuffer
    void clear(const api::render::ClearOptions &options)                      override;
    void bindDefaultRenderTarget()                                             override;
    void setViewport(std::uint32_t width, std::uint32_t height)               override;

    // Pipeline state
    void setFillMode(api::render::FillMode mode)                              override;
    void setDepthTest(bool enabled)                                           override;
    void setDepthWrite(bool enabled)                                          override;
    void setDepthFunc(api::render::DepthFunction func)                        override;
    void setCull(api::render::CullMode mode)                                  override;
    void setBlend(bool enabled)                                               override;
    void setBlendFunc(api::render::BlendFactor src, api::render::BlendFactor dst) override;
    void setSRGB(bool enabled)                                                override;

    // Resource creation
    [[nodiscard]] std::unique_ptr<api::render::IGpuBuffer> createBuffer(
        api::render::BufferType type, const void *data, std::size_t size)     override;

    [[nodiscard]] std::unique_ptr<api::render::IVertexInputState> createVertexInputState(
        const api::render::VertexLayout &layout,
        const api::render::IGpuBuffer   &vertexBuffer,
        const api::render::IGpuBuffer   &indexBuffer)                         override;

    // Uniforms & drawing
    void setUniform(UniformLocation location, const core::UniformValue &value) override;
    void drawIndexed(std::size_t indexCount)                                   override;

    // Factories
    [[nodiscard]] api::render::IShaderCompiler     &shaderCompiler()       override { return m_shaderCompiler; }
    [[nodiscard]] api::render::ITextureFactory     &textureFactory()       override { return m_textureFactory; }
    [[nodiscard]] api::render::IRenderTargetFactory &renderTargetFactory() override { return m_renderTargetFactory; }
    [[nodiscard]] api::render::IGpuMeshFactory     &gpuMeshFactory()       override { return m_gpuMeshFactory; }

private:
    void setupVertexAttributes(const api::render::VertexLayout &layout);

    GlShaderCompiler      m_shaderCompiler;
    GlTextureFactory      m_textureFactory;
    GlRenderTargetFactory m_renderTargetFactory{m_textureFactory};
    GlGpuMeshFactory      m_gpuMeshFactory{*this};
};

} // namespace sonnet::renderer::opengl
