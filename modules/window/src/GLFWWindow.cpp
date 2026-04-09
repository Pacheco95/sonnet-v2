#include <sonnet/window/GLFWWindow.h>

#include <stdexcept>

namespace sonnet::window {

GLFWWindow::GLFWWindow(const WindowConfig &cfg) : m_title(cfg.title) {
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    m_window = glfwCreateWindow(cfg.width, cfg.height, cfg.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync

    setupCallbacks();
    syncSize();
}

GLFWWindow::~GLFWWindow() {
    if (glfwGetCurrentContext() == m_window) {
        glfwMakeContextCurrent(nullptr);
    }
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void GLFWWindow::setTitle(const std::string &title) {
    m_title = title;
    glfwSetWindowTitle(m_window, title.c_str());
}

const std::string &GLFWWindow::getTitle() const { return m_title; }

glm::uvec2 GLFWWindow::getFrameBufferSize() const { return m_framebufferSize; }

void GLFWWindow::setVisible(bool visible) {
    if (visible) glfwShowWindow(m_window);
    else         glfwHideWindow(m_window);
}

bool GLFWWindow::isVisible() const {
    return glfwGetWindowAttrib(m_window, GLFW_VISIBLE) != 0;
}

void GLFWWindow::requestClose()  { m_shouldClose = true; }
bool GLFWWindow::shouldClose()   const { return m_shouldClose || glfwWindowShouldClose(m_window); }

void GLFWWindow::pollEvents()    { glfwPollEvents(); }
void GLFWWindow::swapBuffers()   { glfwSwapBuffers(m_window); }

void GLFWWindow::toggleFullscreen() {
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);

    if (!m_fullscreen) {
        glfwGetWindowPos(m_window, &m_savedPos.x,  &m_savedPos.y);
        glfwGetWindowSize(m_window, &m_savedSize.x, &m_savedSize.y);
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(m_window, nullptr, m_savedPos.x, m_savedPos.y, m_savedSize.x, m_savedSize.y, 0);
    }

    m_fullscreen = !m_fullscreen;
    syncSize();
}

void GLFWWindow::captureCursor() { glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
void GLFWWindow::releaseCursor() { glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }

api::window::WindowState GLFWWindow::getState() const { return m_state; }

// ── Private ───────────────────────────────────────────────────────────────────

void GLFWWindow::syncSize() {
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    m_framebufferSize = {static_cast<unsigned>(w), static_cast<unsigned>(h)};
}

void GLFWWindow::setupCallbacks() {
    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window,             cbKey);
    glfwSetMouseButtonCallback(m_window,     cbMouseButton);
    glfwSetCursorPosCallback(m_window,       cbMouseMove);
    glfwSetFramebufferSizeCallback(m_window, cbFramebufferSize);
    glfwSetWindowFocusCallback(m_window,     cbFocus);
    glfwSetWindowIconifyCallback(m_window,   cbIconify);
    glfwSetWindowMaximizeCallback(m_window,  cbMaximize);
    glfwSetWindowCloseCallback(m_window,     cbClose);
}

// Callbacks

void GLFWWindow::cbKey(GLFWwindow *w, int key, int sc, int action, int mods) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    if (self->m_inputAdapter) self->m_inputAdapter->onKey(w, key, sc, action, mods);
}

void GLFWWindow::cbMouseButton(GLFWwindow *w, int button, int action, int mods) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    if (self->m_inputAdapter) self->m_inputAdapter->onMouseButton(w, button, action, mods);
}

void GLFWWindow::cbMouseMove(GLFWwindow *w, double x, double y) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    if (self->m_inputAdapter) self->m_inputAdapter->onMouseMove(w, x, y);
}

void GLFWWindow::cbFramebufferSize(GLFWwindow *w, int width, int height) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    self->m_framebufferSize = {static_cast<unsigned>(width), static_cast<unsigned>(height)};
}

void GLFWWindow::cbFocus(GLFWwindow *w, int focused) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    self->changeState(focused ? api::window::WindowState::Focused : api::window::WindowState::Unfocused);
}

void GLFWWindow::cbIconify(GLFWwindow *w, int iconified) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    self->changeState(iconified ? api::window::WindowState::Minimized : api::window::WindowState::Normal);
}

void GLFWWindow::cbMaximize(GLFWwindow *w, int maximized) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    self->changeState(maximized ? api::window::WindowState::Maximized : api::window::WindowState::Normal);
}

void GLFWWindow::cbClose(GLFWwindow *w) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    self->changeState(api::window::WindowState::Closed);
}

} // namespace sonnet::window
