#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace sonnet::renderer::vulkan {

class Device;

// Per-frame-in-flight command resources: a command pool, its primary command
// buffer, and two sync primitives (imageAvailable + inFlightFence).
// renderFinished lives on Swapchain (per-image), not here. Double-buffered
// with kFramesInFlight = 2.
class CommandContext {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;

    explicit CommandContext(Device &device);
    ~CommandContext();

    CommandContext(const CommandContext &) = delete;
    CommandContext &operator=(const CommandContext &) = delete;

    struct FrameData {
        VkCommandPool   pool            = VK_NULL_HANDLE;
        VkCommandBuffer cmd             = VK_NULL_HANDLE;
        VkFence         inFlight        = VK_NULL_HANDLE;
        VkSemaphore     imageAvailable  = VK_NULL_HANDLE;
    };

    // Advance to the next frame slot, wait for its fence, reset its pool.
    // The returned FrameData is the one the caller should record into.
    FrameData &beginFrame();
    // Move to the next frame slot. Call after queue submit + present complete.
    void advance() { m_frameIx = (m_frameIx + 1) % kFramesInFlight; }

    [[nodiscard]] std::uint32_t currentFrameIndex() const { return m_frameIx; }
    [[nodiscard]] FrameData     &current() { return m_frames[m_frameIx]; }

private:
    Device                                     &m_device;
    std::array<FrameData, kFramesInFlight>      m_frames{};
    std::uint32_t                               m_frameIx = 0;
};

} // namespace sonnet::renderer::vulkan
