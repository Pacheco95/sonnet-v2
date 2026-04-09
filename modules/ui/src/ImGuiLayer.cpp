#include <sonnet/ui/ImGuiLayer.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glad/glad.h>

namespace sonnet::ui {

ImGuiLayer::~ImGuiLayer() {
    if (m_initialized) shutdown();
}

void ImGuiLayer::init(GLFWwindow *window, const char *glslVersion) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    m_initialized = true;
}

void ImGuiLayer::shutdown() {
    if (!m_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiLayer::begin() {
    // ImGui colors are already in sRGB. Disable the automatic linear→sRGB
    // conversion so they are not gamma-encoded a second time.
    m_srgbWasEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Restore the sRGB state for subsequent 3D rendering.
    if (m_srgbWasEnabled) glEnable(GL_FRAMEBUFFER_SRGB);
}

} // namespace sonnet::ui
