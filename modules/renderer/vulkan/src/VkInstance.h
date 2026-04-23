#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace sonnet::renderer::vulkan {

// Owns VkInstance + optional debug-utils messenger.
// macOS/MoltenVK: enables VK_KHR_portability_enumeration and sets the
// PORTABILITY_ENUMERATE_BIT on instance creation.
class Instance {
public:
    Instance(const std::vector<const char *> &glfwExtensions,
             bool enableValidation);
    ~Instance();

    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;

    [[nodiscard]] VkInstance handle()   const { return m_instance; }
    [[nodiscard]] bool validationOn()   const { return m_validationEnabled; }

private:
    VkInstance               m_instance  = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    bool                     m_validationEnabled = false;
    bool                     m_portabilityEnabled = false;

    static std::vector<const char *> selectLayers(bool enableValidation);
    void createDebugMessenger();
    void destroyDebugMessenger();
};

} // namespace sonnet::renderer::vulkan
