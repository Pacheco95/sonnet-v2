#pragma once

// Forward-declare GLFWwindow so consumers don't need to include GLFW.
struct GLFWwindow;

namespace sonnet::ui {

// RAII wrapper around the ImGui GLFW + OpenGL3 backends.
// Call init() once after the GL context is ready, then begin()/end() each frame.
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    // Non-copyable, non-movable (owns ImGui context).
    ImGuiLayer(const ImGuiLayer &) = delete;
    ImGuiLayer &operator=(const ImGuiLayer &) = delete;

    void init(GLFWwindow *window, const char *glslVersion = "#version 330");
    void shutdown();

    // Call between renderer clear and swapBuffers().
    void begin();
    void end();

    [[nodiscard]] bool initialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace sonnet::ui
