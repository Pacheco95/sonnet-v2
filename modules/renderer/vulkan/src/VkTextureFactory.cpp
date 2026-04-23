#include <sonnet/renderer/vulkan/VkTextureFactory.h>

#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

VkTextureFactory::VkTextureFactory(Device &device, SamplerCache &samplers)
    : m_device(device), m_samplers(samplers) {}

std::unique_ptr<api::render::ITexture> VkTextureFactory::create(
    const api::render::TextureDesc &desc,
    const api::render::SamplerDesc &sampler,
    const api::render::CPUTextureBuffer &data) const {
    return std::make_unique<VkTexture2D>(m_device, m_samplers, desc, sampler, data);
}

std::unique_ptr<api::render::ITexture> VkTextureFactory::create(
    const api::render::TextureDesc &/*desc*/,
    const api::render::SamplerDesc &/*sampler*/,
    const api::render::CubeMapFaces &/*faces*/) const {
    SN_VK_TODO("VkTextureFactory::create(CubeMapFaces) — needed by IBL in Phase 7");
}

std::unique_ptr<api::render::ITexture> VkTextureFactory::create(
    const api::render::TextureDesc &desc,
    const api::render::SamplerDesc &sampler) const {
    return std::make_unique<VkTexture2D>(m_device, m_samplers, desc, sampler);
}

} // namespace sonnet::renderer::vulkan
