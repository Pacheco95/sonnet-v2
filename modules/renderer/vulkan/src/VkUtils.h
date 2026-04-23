#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace sonnet::renderer::vulkan {

const char *vkResultToString(VkResult r);

struct VulkanError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define VK_CHECK(expr)                                                    \
    do {                                                                  \
        VkResult _res = (expr);                                           \
        if (_res != VK_SUCCESS) {                                         \
            throw ::sonnet::renderer::vulkan::VulkanError(                \
                std::string("VK_CHECK failed: ") + #expr + " -> " +       \
                ::sonnet::renderer::vulkan::vkResultToString(_res));      \
        }                                                                 \
    } while (0)

#define SN_VK_TODO(msg)                                                   \
    throw ::sonnet::renderer::vulkan::VulkanError(                        \
        std::string("Vulkan TODO: ") + (msg))

} // namespace sonnet::renderer::vulkan
