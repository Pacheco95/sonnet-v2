#include <sonnet/renderer/vulkan/VkRenderTargetFactory.h>

namespace sonnet::renderer::vulkan {

VkRenderTargetFactory::VkRenderTargetFactory(Device &device, SamplerCache &samplers,
                                             BindState &bindState)
    : m_device(device), m_samplers(samplers), m_bindState(bindState) {}

std::unique_ptr<api::render::IRenderTarget> VkRenderTargetFactory::create(
    const api::render::RenderTargetDesc &desc) const {
    return std::make_unique<VkRenderTarget>(m_device, m_samplers, m_bindState, desc);
}

} // namespace sonnet::renderer::vulkan
