#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "VkDevice.h"

#include "VkInstance.h"
#include "VkUtils.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace sonnet::renderer::vulkan {

namespace {

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    [[nodiscard]] bool complete() const { return graphics && present; }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());

    QueueFamilies out;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            out.graphics = i;
        }
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupported);
        if (presentSupported == VK_TRUE) {
            out.present = i;
        }
        if (out.complete()) break;
    }
    return out;
}

bool hasDeviceExtension(VkPhysicalDevice dev, const char *name) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, exts.data());
    return std::any_of(exts.begin(), exts.end(),
                       [&](const VkExtensionProperties &p) {
                           return std::strcmp(p.extensionName, name) == 0;
                       });
}

int deviceScore(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    const auto fam = findQueueFamilies(dev, surface);
    if (!fam.complete()) return -1;
    if (!hasDeviceExtension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) return -1;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(dev, &props);

    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;
    score += static_cast<int>(props.limits.maxImageDimension2D);
    return score;
}

} // namespace

Device::Device(Instance &instance, VkSurfaceKHR surface) {
    pickPhysicalDevice(instance, surface);

    // Re-query queue families on the chosen device.
    const auto fam = findQueueFamilies(m_physical, surface);
    m_graphicsFamily = *fam.graphics;
    m_presentFamily  = *fam.present;

    createLogicalDevice();
    createAllocator(instance);
    createOneShotPool();
}

Device::~Device() {
    if (m_oneShotPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_oneShotPool, nullptr);
    if (m_allocator   != VK_NULL_HANDLE) vmaDestroyAllocator(m_allocator);
    if (m_device      != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
}

void Device::pickPhysicalDevice(Instance &instance, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance.handle(), &count, nullptr);
    if (count == 0) {
        throw VulkanError("no Vulkan-capable physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance.handle(), &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (auto dev : devices) {
        const int s = deviceScore(dev, surface);
        if (s > bestScore) { bestScore = s; best = dev; }
    }
    if (best == VK_NULL_HANDLE || bestScore < 0) {
        throw VulkanError("no suitable Vulkan physical device (needs graphics + present + swapchain)");
    }
    m_physical = best;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physical, &props);
    spdlog::info("[vulkan] physical device: {} (API {}.{}.{})",
                 props.deviceName,
                 VK_VERSION_MAJOR(props.apiVersion),
                 VK_VERSION_MINOR(props.apiVersion),
                 VK_VERSION_PATCH(props.apiVersion));

    if (hasDeviceExtension(m_physical, "VK_KHR_portability_subset")) {
        m_portabilitySubsetEnabled = true;
        spdlog::info("[vulkan] VK_KHR_portability_subset present; enabling (MoltenVK/portability path)");
    }
}

void Device::createLogicalDevice() {
    const float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;

    VkDeviceQueueCreateInfo g{};
    g.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    g.queueFamilyIndex = m_graphicsFamily;
    g.queueCount       = 1;
    g.pQueuePriorities = &queuePriority;
    queueInfos.push_back(g);

    if (m_presentFamily != m_graphicsFamily) {
        VkDeviceQueueCreateInfo p = g;
        p.queueFamilyIndex = m_presentFamily;
        queueInfos.push_back(p);
    }

    std::vector<const char *> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (m_portabilitySubsetEnabled) {
        extensions.push_back("VK_KHR_portability_subset");
    }

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount    = static_cast<std::uint32_t>(queueInfos.size());
    info.pQueueCreateInfos       = queueInfos.data();
    info.enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.pEnabledFeatures        = &features;

    VK_CHECK(vkCreateDevice(m_physical, &info, nullptr, &m_device));

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily,  0, &m_presentQueue);

    spdlog::info("[vulkan] logical device created "
                 "(graphicsFamily={}, presentFamily={}, extensions={})",
                 m_graphicsFamily, m_presentFamily, extensions.size());
}

void Device::createAllocator(Instance &instance) {
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = m_physical;
    info.device         = m_device;
    info.instance       = instance.handle();
    info.vulkanApiVersion = VK_API_VERSION_1_2;
    VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
}

void Device::waitIdle() const {
    if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
}

void Device::createOneShotPool() {
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                          | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = m_graphicsFamily;
    VK_CHECK(vkCreateCommandPool(m_device, &info, nullptr, &m_oneShotPool));
}

void Device::runOneShot(const std::function<void(VkCommandBuffer)> &recorder) {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = m_oneShotPool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &alloc, &cmd));

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    recorder(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));

    vkFreeCommandBuffers(m_device, m_oneShotPool, 1, &cmd);
}

} // namespace sonnet::renderer::vulkan
