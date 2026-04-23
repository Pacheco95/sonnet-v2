#include "VkInstance.h"

#include "VkUtils.h"

#include <algorithm>
#include <cstring>

namespace sonnet::renderer::vulkan {

namespace {

constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool hasInstanceExtension(const std::vector<VkExtensionProperties> &available,
                          const char *name) {
    return std::any_of(available.begin(), available.end(),
                       [&](const VkExtensionProperties &p) {
                           return std::strcmp(p.extensionName, name) == 0;
                       });
}

bool hasInstanceLayer(const std::vector<VkLayerProperties> &available,
                      const char *name) {
    return std::any_of(available.begin(), available.end(),
                       [&](const VkLayerProperties &p) {
                           return std::strcmp(p.layerName, name) == 0;
                       });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT        /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void * /*userData*/) {
    const char *msg = data && data->pMessage ? data->pMessage : "(no message)";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        spdlog::error("[vulkan] {}", msg);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("[vulkan] {}", msg);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        spdlog::info("[vulkan] {}", msg);
    } else {
        spdlog::debug("[vulkan] {}", msg);
    }
    return VK_FALSE;
}

} // namespace

std::vector<const char *> Instance::selectLayers(bool enableValidation) {
    std::vector<const char *> layers;
    if (!enableValidation) return layers;

    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    if (hasInstanceLayer(available, kValidationLayer)) {
        layers.push_back(kValidationLayer);
    } else {
        spdlog::warn("[vulkan] validation layer {} not available", kValidationLayer);
    }
    return layers;
}

Instance::Instance(const std::vector<const char *> &glfwExtensions,
                   bool enableValidation) {
    std::uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());

    std::vector<const char *> extensions = glfwExtensions;
    if (enableValidation &&
        hasInstanceExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        m_validationEnabled = true;
    }

    VkInstanceCreateFlags flags = 0;
    if (hasInstanceExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        if (hasInstanceExtension(available, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        m_portabilityEnabled = true;
    }

    const auto layers = selectLayers(enableValidation);

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "Sonnet";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.pEngineName        = "Sonnet";
    appInfo.engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.flags                   = flags;
    info.pApplicationInfo        = &appInfo;
    info.enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    info.enabledLayerCount       = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames     = layers.empty()     ? nullptr : layers.data();

    VK_CHECK(vkCreateInstance(&info, nullptr, &m_instance));

    spdlog::info("[vulkan] instance created "
                 "(validation={}, portability={}, extensions={}, layers={})",
                 m_validationEnabled, m_portabilityEnabled,
                 extensions.size(), layers.size());

    if (m_validationEnabled) {
        createDebugMessenger();
    }
}

Instance::~Instance() {
    destroyDebugMessenger();
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void Instance::createDebugMessenger() {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!create) {
        spdlog::warn("[vulkan] vkCreateDebugUtilsMessengerEXT unavailable");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;

    VK_CHECK(create(m_instance, &info, nullptr, &m_messenger));
}

void Instance::destroyDebugMessenger() {
    if (m_messenger == VK_NULL_HANDLE) return;
    auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy) destroy(m_instance, m_messenger, nullptr);
    m_messenger = VK_NULL_HANDLE;
}

} // namespace sonnet::renderer::vulkan
