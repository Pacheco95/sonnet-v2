#include <sonnet/ui/ImGuiLayer.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#if defined(SONNET_USE_OPENGL)
#  include <imgui_impl_opengl3.h>
#  include <glad/glad.h>
#endif

#include <stdexcept>

namespace sonnet::ui {

ImGuiLayer::~ImGuiLayer() {
    if (m_initialized) shutdown();
}

void ImGuiLayer::init([[maybe_unused]] GLFWwindow *window,
                      [[maybe_unused]] const char *glslVersion) {
#if defined(SONNET_USE_OPENGL)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    m_initialized = true;
#else
    // Vulkan: ImGui wiring comes online in Phase 4.
    throw std::runtime_error("ImGuiLayer::init — Vulkan ImGui integration lands in Phase 4");
#endif
}

void ImGuiLayer::shutdown() {
    if (!m_initialized) return;
#if defined(SONNET_USE_OPENGL)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    m_initialized = false;
}

void ImGuiLayer::begin() {
#if defined(SONNET_USE_OPENGL)
    // ImGui colors are already in sRGB. Disable the automatic linear→sRGB
    // conversion so they are not gamma-encoded a second time.
    m_srgbWasEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif
}

void ImGuiLayer::end() {
#if defined(SONNET_USE_OPENGL)
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Restore the sRGB state for subsequent 3D rendering.
    if (m_srgbWasEnabled) glEnable(GL_FRAMEBUFFER_SRGB);
#endif
}

} // namespace sonnet::ui
