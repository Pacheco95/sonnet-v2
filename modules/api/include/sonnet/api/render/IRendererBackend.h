#pragma once

#include <sonnet/api/render/GpuMesh.h>
#include <sonnet/api/render/IGpuBuffer.h>
#include <sonnet/api/render/IRenderTarget.h>
#include <sonnet/api/render/IShader.h>
#include <sonnet/api/render/ITexture.h>
#include <sonnet/api/render/IVertexInputState.h>
#include <sonnet/api/render/RenderState.h>
#include <sonnet/api/render/VertexLayout.h>
#include <sonnet/core/RendererTraits.h>
#include <sonnet/core/Types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace sonnet::api::render {

// Forward declarations so IRendererBackend can reference factory interfaces.
class IShaderCompiler;
class ITextureFactory;
class IRenderTargetFactory;
class IGpuMeshFactory;

struct ColorClear {
    std::uint32_t attachmentIndex;
    glm::vec4     value;
};

struct ClearOptions {
    std::vector<ColorClear>   colors;
    std::optional<float>      depth;
    std::optional<std::uint32_t> stencil;
};

class IRendererBackend {
public:
    using UniformLocation = std::int32_t;

    virtual ~IRendererBackend() = default;

    virtual void initialize() = 0;

    // Frame lifecycle
    virtual void beginFrame() = 0;
    virtual void endFrame()   = 0;

    // Framebuffer
    virtual void clear(const ClearOptions &options)                        = 0;
    virtual void bindDefaultRenderTarget()                                 = 0;
    virtual void bindRenderTarget(const IRenderTarget &target)             = 0;
    virtual void setViewport(std::uint32_t width, std::uint32_t height)   = 0;

    // Pipeline state
    virtual void setFillMode(FillMode mode)       = 0;
    virtual void setDepthTest(bool enabled)       = 0;
    virtual void setDepthWrite(bool enabled)      = 0;
    virtual void setDepthFunc(DepthFunction func) = 0;
    virtual void setCull(CullMode mode)           = 0;
    virtual void setBlend(bool enabled)           = 0;
    virtual void setBlendFunc(BlendFactor src, BlendFactor dst) = 0;
    virtual void setSRGB(bool enabled)            = 0;

    // Resource creation
    virtual std::unique_ptr<IGpuBuffer> createBuffer(
        BufferType type, const void *data, std::size_t size) = 0;

    virtual std::unique_ptr<IVertexInputState> createVertexInputState(
        const VertexLayout &layout,
        const IGpuBuffer   &vertexBuffer,
        const IGpuBuffer   &indexBuffer) = 0;

    // Uniforms & drawing
    virtual void setUniform(UniformLocation location, const core::UniformValue &value) = 0;
    virtual void drawIndexed(std::size_t indexCount) = 0;

    // Factories (owned by the backend)
    [[nodiscard]] virtual IShaderCompiler  &shaderCompiler()       = 0;
    [[nodiscard]] virtual ITextureFactory  &textureFactory()       = 0;
    [[nodiscard]] virtual IRenderTargetFactory &renderTargetFactory() = 0;
    [[nodiscard]] virtual IGpuMeshFactory  &gpuMeshFactory()       = 0;

    // Backend identity for math/NDC corrections (clip-space Y flip, Z range).
    [[nodiscard]] virtual const core::RendererTraits &traits() const = 0;
};

// Forward-declared factory interfaces (defined alongside their products)
class ITextureFactory {
public:
    virtual ~ITextureFactory() = default;
    // Create from CPU data.
    [[nodiscard]] virtual std::unique_ptr<ITexture> create(
        const TextureDesc &desc, const SamplerDesc &sampler,
        const CPUTextureBuffer &data) const = 0;
    // Create from cubemap faces.
    [[nodiscard]] virtual std::unique_ptr<ITexture> create(
        const TextureDesc &desc, const SamplerDesc &sampler,
        const CubeMapFaces &faces) const = 0;
    // Allocate without data (for render targets).
    [[nodiscard]] virtual std::unique_ptr<ITexture> create(
        const TextureDesc &desc, const SamplerDesc &sampler) const = 0;
};

class IRenderTargetFactory {
public:
    virtual ~IRenderTargetFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<IRenderTarget> create(
        const RenderTargetDesc &desc) const = 0;
};

} // namespace sonnet::api::render
