#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace sonnet::renderer::vulkan {

class Device;
class VkShader;
struct BindState;

// Manages per-frame descriptor pools + allocations for set=0 (frame-wide
// UBOs like Camera + Lights). Each frame gets its own VkDescriptorPool;
// beginFrame() resets the current frame's pool. drawIndexed() asks the
// manager to allocate and populate a set=0 descriptor set for the active
// shader from whatever IGpuBuffer::bindBase calls have written to BindState.
//
// Phase 3c scope is set=0 only. Per-material set=1 (textures) and per-draw
// set=2 (ring-buffer UBO) land in Phase 3d/3e.
class DescriptorManager {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;

    DescriptorManager(Device &device, BindState &bindState);
    ~DescriptorManager();

    DescriptorManager(const DescriptorManager &)            = delete;
    DescriptorManager &operator=(const DescriptorManager &) = delete;

    // Called at beginFrame before recording draws into the given frame slot.
    void beginFrame(std::uint32_t frameIx);

    // Allocate+update a set=0 descriptor set for `shader` populated from the
    // active BindState.ubos[]. Returns VK_NULL_HANDLE if the shader has no
    // set=0 bindings, in which case drawIndexed should skip the bind step.
    VkDescriptorSet allocateFrameSet0(const VkShader &shader);

    // Allocate+update a set=1 descriptor set for `shader`, populated from the
    // materialTextures slots recorded by VkTexture2D::bind. Returns NULL when
    // the shader has no set=1 bindings.
    VkDescriptorSet allocateMaterialSet1(const VkShader &shader);

    // Allocate+update a set=2 descriptor set for `shader`, pointing the
    // single UBO binding at the given ring buffer + offset + range. Returns
    // NULL when the shader has no set=2 bindings.
    VkDescriptorSet allocatePerDrawSet2(const VkShader &shader,
                                        VkBuffer ringBuffer,
                                        VkDeviceSize offset,
                                        VkDeviceSize range);

private:
    Device    &m_device;
    BindState &m_bindState;

    std::array<VkDescriptorPool, kFramesInFlight> m_pools{};
    std::uint32_t                                 m_currentFrame = 0;
};

} // namespace sonnet::renderer::vulkan
