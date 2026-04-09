#pragma once

#include <sonnet/api/window/IWindow.h>
#include <sonnet/window/GLFWInputAdapter.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>

namespace sonnet::window {

struct WindowConfig {
    int         width  = 1280;
    int         height = 720;
    std::string title  = "Sonnet";
};

class GLFWWindow final : public api::window::IWindow {
public:
    explicit GLFWWindow(const WindowConfig &config);
    ~GLFWWindow() override;

    // IWindow
    void                               setTitle(const std::string &title) override;
    [[nodiscard]] const std::string   &getTitle()         const override;
    [[nodiscard]] glm::uvec2           getFrameBufferSize() const override;
    void                               setVisible(bool visible) override;
    [[nodiscard]] bool                 isVisible()        const override;
    void                               requestClose()     override;
    [[nodiscard]] bool                 shouldClose()      const override;
    void                               pollEvents()       override;
    void                               swapBuffers()      override;
    void                               toggleFullscreen() override;
    void                               captureCursor()    override;
    void                               releaseCursor()    override;
    [[nodiscard]] api::window::WindowState getState()     const override;

    // Attach an input adapter (optional — events are discarded if not set).
    void setInputAdapter(GLFWInputAdapter *adapter) { m_inputAdapter = adapter; }

    [[nodiscard]] GLFWwindow *handle() const { return m_window; }

private:
    void setupCallbacks();
    void syncSize();
    void changeState(api::window::WindowState s) { m_state = s; }

    // GLFW callbacks
    static void cbKey(GLFWwindow *, int key, int scancode, int action, int mods);
    static void cbMouseButton(GLFWwindow *, int button, int action, int mods);
    static void cbMouseMove(GLFWwindow *, double x, double y);
    static void cbFramebufferSize(GLFWwindow *, int w, int h);
    static void cbFocus(GLFWwindow *, int focused);
    static void cbIconify(GLFWwindow *, int iconified);
    static void cbMaximize(GLFWwindow *, int maximized);
    static void cbClose(GLFWwindow *);

    GLFWwindow                     *m_window       = nullptr;
    GLFWInputAdapter               *m_inputAdapter = nullptr;
    api::window::WindowState        m_state        = api::window::WindowState::Normal;
    std::string                     m_title;
    glm::uvec2                      m_framebufferSize{};
    bool                            m_shouldClose  = false;
    bool                            m_fullscreen   = false;
    glm::ivec2                      m_savedPos{};
    glm::ivec2                      m_savedSize{};
};

} // namespace sonnet::window
