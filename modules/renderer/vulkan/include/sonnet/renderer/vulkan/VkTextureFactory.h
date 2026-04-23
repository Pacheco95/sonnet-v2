#pragma once

#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include <memory>

namespace sonnet::renderer::vulkan {

class Device;
class SamplerCache;

class VkTextureFactory final : public api::render::ITextureFactory {
public:
    VkTextureFactory(Device &device, SamplerCache &samplers);

    [[nodiscard]] std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &desc,
        const api::render::SamplerDesc &sampler,
        const api::render::CPUTextureBuffer &data) const override;

    [[nodiscard]] std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &desc,
        const api::render::SamplerDesc &sampler,
        const api::render::CubeMapFaces &faces) const override;

    [[nodiscard]] std::unique_ptr<api::render::ITexture> create(
        const api::render::TextureDesc &desc,
        const api::render::SamplerDesc &sampler) const override;

private:
    Device       &m_device;
    SamplerCache &m_samplers;
};

} // namespace sonnet::renderer::vulkan
