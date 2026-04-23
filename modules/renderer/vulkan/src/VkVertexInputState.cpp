#include <sonnet/renderer/vulkan/VkVertexInputState.h>

#include "VkBindState.h"
#include "VkUtils.h"

#include <cstdint>

namespace sonnet::renderer::vulkan {

namespace {

// Map a glm vector/integer type (embedded in a VertexAttribute's T) to a
// VkFormat. Pattern-matched on the VertexAttribute's typedef template
// parameter via std::visit in build().
template <typename T>
constexpr VkFormat vertexFormatFor() {
    if constexpr (std::is_same_v<T, glm::vec2>)  return VK_FORMAT_R32G32_SFLOAT;
    else if constexpr (std::is_same_v<T, glm::vec3>)  return VK_FORMAT_R32G32B32_SFLOAT;
    else if constexpr (std::is_same_v<T, glm::vec4>)  return VK_FORMAT_R32G32B32A32_SFLOAT;
    else if constexpr (std::is_same_v<T, glm::ivec4>) return VK_FORMAT_R32G32B32A32_SINT;
    else {
        static_assert(sizeof(T) == 0, "Unsupported vertex attribute type");
        return VK_FORMAT_UNDEFINED;
    }
}

} // namespace

VkVertexInputState::VkVertexInputState(const api::render::VertexLayout &layout,
                                       BindState &bindState)
    : m_bindState(bindState) {
    m_binding.binding   = 0;
    m_binding.stride    = static_cast<std::uint32_t>(layout.getStride());
    m_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::uint32_t offset = 0;
    for (const auto &attr : layout.getAttributes()) {
        std::visit([&](auto &&a) {
            using A = std::decay_t<decltype(a)>;
            VkVertexInputAttributeDescription desc{};
            desc.location = A::location;
            desc.binding  = 0;
            desc.format   = vertexFormatFor<typename A::ValueType>();
            desc.offset   = offset;
            m_attributes.push_back(desc);
            offset += A::sizeInBytes;
        }, attr);
    }
}

void VkVertexInputState::bind()   const { m_bindState.currentVertexInput = this; }
void VkVertexInputState::unbind() const {
    if (m_bindState.currentVertexInput == this) m_bindState.currentVertexInput = nullptr;
}

} // namespace sonnet::renderer::vulkan
