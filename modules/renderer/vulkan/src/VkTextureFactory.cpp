#include <sonnet/renderer/vulkan/VkTextureFactory.h>

#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

VkTextureFactory::VkTextureFactory(Device &device, SamplerCache &samplers, BindState &bindState)
    : m_device(device), m_samplers(samplers), m_bindState(bindState) {}

std::unique_ptr<api::render::ITexture> VkTextureFactory::create(
    const api::render::TextureDesc &desc,
    const api::render::SamplerDesc &sampler,
    const api::render::CPUTextureBuffer &data) const {
    return std::make_unique<VkTexture2D>(m_device, m_samplers, m_bindState, desc, sampler, data);
}

std::unique_ptr<api::render::ITexture> VkTextureFactory::create(
    const api::render::TextureDesc &desc,
    const api::render::SamplerDesc &sampler,
    const api::render::CubeMapFaces &faces) const {
    return std::make_unique<VkTexture2D>(m_device, m_samplers, m_bindState,
                                          desc, sampler, faces);
}

std::unique_ptr<api::render::ITexture> VkTextureFactory::create(
    const api::render::TextureDesc &desc,
    const api::render::SamplerDesc &sampler) const {
    return std::make_unique<VkTexture2D>(m_device, m_samplers, m_bindState, desc, sampler);
}

} // namespace sonnet::renderer::vulkan
