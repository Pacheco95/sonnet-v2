#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace sonnet::renderer::vulkan {

// Mutable state shared between IGpuBuffer::bind()/bindBase() call sites and
// the backend's drawIndexed(). Tracks "which VkBuffer is the current vertex
// buffer / index buffer" and which VkBuffer is bound to each UBO slot. Owned
// by VkRendererBackend; referenced by every VkGpuBuffer it creates.
//
// This is a Phase-2 stand-in for a proper descriptor-set-layout-aware
// binding tracker, which lands in Phase 3 alongside the pipeline cache.
struct BindState {
    static constexpr std::uint32_t kMaxUboBindings = 8;

    VkBuffer                              currentVertex = VK_NULL_HANDLE;
    VkBuffer                              currentIndex  = VK_NULL_HANDLE;
    VkIndexType                           indexType     = VK_INDEX_TYPE_UINT32;
    std::array<VkBuffer, kMaxUboBindings> ubos{};
    std::array<VkDeviceSize, kMaxUboBindings> uboSizes{};

    void reset() {
        currentVertex = VK_NULL_HANDLE;
        currentIndex  = VK_NULL_HANDLE;
        ubos.fill(VK_NULL_HANDLE);
        uboSizes.fill(0);
    }
};

} // namespace sonnet::renderer::vulkan
