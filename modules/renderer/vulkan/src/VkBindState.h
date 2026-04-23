#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace sonnet::renderer::vulkan {

class VkShader;
class VkVertexInputState;

// Mutable state the backend's drawIndexed() reads to assemble a draw:
// which buffers are bound, which shader is active, which vertex input is
// current, and (Phase 3c) which UBOs populate set=0. IGpuBuffer::bind,
// IGpuBuffer::bindBase, IShader::bind, and IVertexInputState::bind all write
// here. Owned by VkRendererBackend; passed by reference to the objects that
// mutate it. This is Vulkan's emulation of OpenGL's context-binding model.
struct BindState {
    static constexpr std::uint32_t kMaxUboBindings = 8;

    VkBuffer                              currentVertex = VK_NULL_HANDLE;
    VkBuffer                              currentIndex  = VK_NULL_HANDLE;
    VkIndexType                           indexType     = VK_INDEX_TYPE_UINT32;
    std::array<VkBuffer, kMaxUboBindings> ubos{};
    std::array<VkDeviceSize, kMaxUboBindings> uboSizes{};

    const VkShader           *currentShader     = nullptr;
    const VkVertexInputState *currentVertexInput = nullptr;

    void reset() {
        currentVertex      = VK_NULL_HANDLE;
        currentIndex       = VK_NULL_HANDLE;
        currentShader      = nullptr;
        currentVertexInput = nullptr;
        ubos.fill(VK_NULL_HANDLE);
        uboSizes.fill(0);
    }
};

} // namespace sonnet::renderer::vulkan
