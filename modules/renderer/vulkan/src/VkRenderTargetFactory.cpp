#include <sonnet/renderer/vulkan/VkRenderTargetFactory.h>

namespace sonnet::renderer::vulkan {

VkRenderTargetFactory::VkRenderTargetFactory(Device &device, SamplerCache &samplers)
    : m_device(device), m_samplers(samplers) {}

std::unique_ptr<api::render::IRenderTarget> VkRenderTargetFactory::create(
    const api::render::RenderTargetDesc &desc) const {
    return std::make_unique<VkRenderTarget>(m_device, m_samplers, desc);
}

} // namespace sonnet::renderer::vulkan
