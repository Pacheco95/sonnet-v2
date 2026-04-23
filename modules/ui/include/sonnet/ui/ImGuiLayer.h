#pragma once

// Forward-declare GLFWwindow so consumers don't need to include GLFW.
struct GLFWwindow;

#if defined(SONNET_USE_VULKAN)
#  include <vulkan/vulkan.h>
#  include <cstdint>
#endif

namespace sonnet::ui {

#if defined(SONNET_USE_VULKAN)
// Everything imgui_impl_vulkan needs. Filled in by VkRendererBackend and
// passed to ImGuiLayer::init. Kept out of ImGuiLayer's own members because
// after init, ImGui-Vulkan owns these internally.
struct VulkanInitInfo {
    VkInstance       instance;
    VkPhysicalDevice physicalDevice;
    VkDevice         device;
    std::uint32_t    queueFamily;
    VkQueue          queue;
    VkRenderPass     renderPass;
    std::uint32_t    minImageCount;
    std::uint32_t    imageCount;
    VkDescriptorPool descriptorPool;
};
#endif

// RAII wrapper around ImGui's GLFW platform backend + the active renderer
// backend (OpenGL3 under SONNET_USE_OPENGL, Vulkan under SONNET_USE_VULKAN).
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    // Non-copyable, non-movable (owns ImGui context).
    ImGuiLayer(const ImGuiLayer &) = delete;
    ImGuiLayer &operator=(const ImGuiLayer &) = delete;

#if defined(SONNET_USE_OPENGL)
    void init(GLFWwindow *window, const char *glslVersion = "#version 330");
#endif

#if defined(SONNET_USE_VULKAN)
    void init(GLFWwindow *window, const VulkanInitInfo &info);
#endif

    void shutdown();

    // Call between renderer clear and swapBuffers().
    // Under Vulkan, `end` only runs ImGui::Render; the actual draw-data record
    // happens in VkRendererBackend::renderImGui(), which has the command buffer.
    void begin();
    void end();

    [[nodiscard]] bool initialized() const { return m_initialized; }

private:
    bool m_initialized    = false;
    bool m_srgbWasEnabled = false;
};

} // namespace sonnet::ui
