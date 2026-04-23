#pragma once

#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/renderer/vulkan/VkRenderTarget.h>

#include <memory>

namespace sonnet::renderer::vulkan {

class Device;
class SamplerCache;
struct BindState;

class VkRenderTargetFactory final : public api::render::IRenderTargetFactory {
public:
    VkRenderTargetFactory(Device &device, SamplerCache &samplers, BindState &bindState);

    [[nodiscard]] std::unique_ptr<api::render::IRenderTarget> create(
        const api::render::RenderTargetDesc &desc) const override;

private:
    Device       &m_device;
    SamplerCache &m_samplers;
    BindState    &m_bindState;
};

} // namespace sonnet::renderer::vulkan
