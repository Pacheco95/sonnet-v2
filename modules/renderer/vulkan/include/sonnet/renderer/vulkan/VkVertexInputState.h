#pragma once

#include <sonnet/api/render/IVertexInputState.h>
#include <sonnet/api/render/VertexLayout.h>

#include <vulkan/vulkan.h>

#include <vector>

namespace sonnet::renderer::vulkan {

struct BindState;

// Vulkan has no VAO equivalent — vertex input is baked into each VkPipeline
// at pipeline-creation time. This class holds the Vulkan descriptor structs
// consumed by VkGraphicsPipelineCreateInfo.pVertexInputState. bind/unbind
// record "this is current" into the shared BindState; drawIndexed reads
// that to look up pipelines.
class VkVertexInputState final : public api::render::IVertexInputState {
public:
    VkVertexInputState(const api::render::VertexLayout &layout, BindState &bindState);

    void bind()   const override; // sets bindState.currentVertexInput = this
    void unbind() const override;

    [[nodiscard]] const VkVertexInputBindingDescription                &bindingDescription()    const { return m_binding; }
    [[nodiscard]] const std::vector<VkVertexInputAttributeDescription> &attributeDescriptions() const { return m_attributes; }

private:
    BindState                                     &m_bindState;
    VkVertexInputBindingDescription                m_binding{};
    std::vector<VkVertexInputAttributeDescription> m_attributes;
};

} // namespace sonnet::renderer::vulkan
