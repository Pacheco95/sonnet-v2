#include "VkCommandContext.h"

#include "VkDevice.h"
#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

CommandContext::CommandContext(Device &device) : m_device(device) {
    VkDevice d = device.logical();

    for (auto &f : m_frames) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = device.graphicsFamily();
        VK_CHECK(vkCreateCommandPool(d, &poolInfo, nullptr, &f.pool));

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool        = f.pool;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(d, &alloc, &f.cmd));

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(d, &fenceInfo, nullptr, &f.inFlight));

        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(d, &sem, nullptr, &f.imageAvailable));
    }
}

CommandContext::~CommandContext() {
    VkDevice d = m_device.logical();
    for (auto &f : m_frames) {
        if (f.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(d, f.imageAvailable, nullptr);
        if (f.inFlight       != VK_NULL_HANDLE) vkDestroyFence(d, f.inFlight, nullptr);
        if (f.pool           != VK_NULL_HANDLE) vkDestroyCommandPool(d, f.pool, nullptr);
    }
}

CommandContext::FrameData &CommandContext::beginFrame() {
    auto &f = m_frames[m_frameIx];
    VkDevice d = m_device.logical();

    VK_CHECK(vkWaitForFences(d, 1, &f.inFlight, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(d, 1, &f.inFlight));
    VK_CHECK(vkResetCommandPool(d, f.pool, 0));
    return f;
}

} // namespace sonnet::renderer::vulkan
