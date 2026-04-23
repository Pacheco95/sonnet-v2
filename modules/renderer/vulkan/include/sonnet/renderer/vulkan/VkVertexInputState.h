#pragma once

#include <sonnet/api/render/IVertexInputState.h>
#include <sonnet/api/render/VertexLayout.h>

#include <vulkan/vulkan.h>

#include <vector>

namespace sonnet::renderer::vulkan {

// Vulkan has no VAO equivalent — vertex input is baked into each VkPipeline
// at pipeline-creation time. This class is therefore a pure data holder:
// it converts the engine's VertexLayout into the Vulkan descriptor structs
// consumed by VkGraphicsPipelineCreateInfo.pVertexInputState. bind/unbind
// are no-ops kept for IVertexInputState API compatibility.
class VkVertexInputState final : public api::render::IVertexInputState {
public:
    explicit VkVertexInputState(const api::render::VertexLayout &layout);

    void bind()   const override; // no-op
    void unbind() const override; // no-op

    [[nodiscard]] const VkVertexInputBindingDescription                &bindingDescription()    const { return m_binding; }
    [[nodiscard]] const std::vector<VkVertexInputAttributeDescription> &attributeDescriptions() const { return m_attributes; }

private:
    VkVertexInputBindingDescription                m_binding{};
    std::vector<VkVertexInputAttributeDescription> m_attributes;
};

} // namespace sonnet::renderer::vulkan
