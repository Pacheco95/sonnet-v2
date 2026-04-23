#include <sonnet/ui/ImGuiLayer.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#if defined(SONNET_USE_OPENGL)
#  include <imgui_impl_opengl3.h>
#  include <glad/glad.h>
#endif

#if defined(SONNET_USE_VULKAN)
#  include <imgui_impl_vulkan.h>
#endif

#include <stdexcept>

namespace sonnet::ui {

ImGuiLayer::~ImGuiLayer() {
    if (m_initialized) shutdown();
}

namespace {
void setupImGuiContext() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui::StyleColorsDark();
}
} // namespace

#if defined(SONNET_USE_OPENGL)
void ImGuiLayer::init(GLFWwindow *window, const char *glslVersion) {
    setupImGuiContext();
    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    m_initialized = true;
}
#endif

#if defined(SONNET_USE_VULKAN)
void ImGuiLayer::init(GLFWwindow *window, const VulkanInitInfo &info) {
    setupImGuiContext();
    ImGui_ImplGlfw_InitForVulkan(window, /*install_callbacks=*/true);

    // The docking branch of ImGui (≥ 2025/09/26) splits pipeline fields into
    // PipelineInfoMain; the top-level ApiVersion is also required.
    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.ApiVersion                 = VK_API_VERSION_1_2;
    vkInfo.Instance                   = info.instance;
    vkInfo.PhysicalDevice             = info.physicalDevice;
    vkInfo.Device                     = info.device;
    vkInfo.QueueFamily                = info.queueFamily;
    vkInfo.Queue                      = info.queue;
    vkInfo.DescriptorPool             = info.descriptorPool;
    vkInfo.MinImageCount              = info.minImageCount;
    vkInfo.ImageCount                 = info.imageCount;
    vkInfo.PipelineInfoMain.RenderPass  = info.renderPass;
    vkInfo.PipelineInfoMain.Subpass     = 0;
    vkInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&vkInfo);

    m_initialized = true;
}
#endif

void ImGuiLayer::shutdown() {
    if (!m_initialized) return;
#if defined(SONNET_USE_OPENGL)
    ImGui_ImplOpenGL3_Shutdown();
#endif
#if defined(SONNET_USE_VULKAN)
    ImGui_ImplVulkan_Shutdown();
#endif
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiLayer::begin() {
#if defined(SONNET_USE_OPENGL)
    // ImGui colors are already in sRGB. Disable automatic linear→sRGB so
    // they are not gamma-encoded a second time.
    m_srgbWasEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);

    ImGui_ImplOpenGL3_NewFrame();
#elif defined(SONNET_USE_VULKAN)
    ImGui_ImplVulkan_NewFrame();
#endif
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end() {
    ImGui::Render();
#if defined(SONNET_USE_OPENGL)
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (m_srgbWasEnabled) glEnable(GL_FRAMEBUFFER_SRGB);
#endif
    // Under Vulkan, VkRendererBackend::renderImGui() picks up the draw data
    // and records it into the current command buffer.
}

} // namespace sonnet::ui
