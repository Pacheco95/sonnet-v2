#include <sonnet/window/GLFWInputAdapter.h>

namespace sonnet::window {

void GLFWInputAdapter::onKey(GLFWwindow *, int glfwKey, int, int action, int) const {
    const auto key = GLFW_KEY_MAP.get(glfwKey);
    if (!key) return;
    m_sink.onKeyEvent({.key = *key, .pressed = (action != GLFW_RELEASE)});
}

void GLFWInputAdapter::onMouseButton(GLFWwindow *, int button, int action, int) const {
    auto it = GLFW_MOUSE_BUTTON_MAP.find(button);
    if (it == GLFW_MOUSE_BUTTON_MAP.end()) return;
    m_sink.onMouseEvent(api::input::MouseButtonEvent{.button = it->second, .pressed = (action == GLFW_PRESS)});
}

void GLFWInputAdapter::onMouseMove(GLFWwindow *, double x, double y) const {
    m_sink.onMouseEvent(api::input::MouseMovedEvent{.position = {static_cast<float>(x), static_cast<float>(y)}});
}

} // namespace sonnet::window
