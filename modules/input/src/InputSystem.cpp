#include <sonnet/input/InputSystem.h>

#include <variant>

namespace sonnet::input {

using namespace api::input;

void InputSystem::onKeyEvent(const KeyEvent &event) {
    m_keys[event.key].current = event.pressed;
}

void InputSystem::onMouseEvent(const MouseEvent &event) {
    std::visit([this](auto &&e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, MouseButtonEvent>) {
            m_mouseButtons[e.button].current = e.pressed;
        } else if constexpr (std::is_same_v<T, MouseMovedEvent>) {
            if (m_hasMousePosition) {
                m_mouseDelta += e.position - m_mousePosition;
            }
            m_mousePosition    = e.position;
            m_hasMousePosition = true;
        }
    }, event);
}

bool InputSystem::isKeyDown(Key key) const {
    auto it = m_keys.find(key);
    return it != m_keys.end() && it->second.current;
}

bool InputSystem::isKeyJustPressed(Key key) const {
    auto it = m_keys.find(key);
    return it != m_keys.end() && it->second.current && !it->second.previous;
}

bool InputSystem::isKeyJustReleased(Key key) const {
    auto it = m_keys.find(key);
    return it != m_keys.end() && !it->second.current && it->second.previous;
}

bool InputSystem::isMouseDown(MouseButton button) const {
    auto it = m_mouseButtons.find(button);
    return it != m_mouseButtons.end() && it->second.current;
}

bool InputSystem::isMouseJustPressed(MouseButton button) const {
    auto it = m_mouseButtons.find(button);
    return it != m_mouseButtons.end() && it->second.current && !it->second.previous;
}

bool InputSystem::isMouseJustReleased(MouseButton button) const {
    auto it = m_mouseButtons.find(button);
    return it != m_mouseButtons.end() && !it->second.current && it->second.previous;
}

glm::vec2 InputSystem::mouseDelta() const {
    return m_mouseDelta;
}

void InputSystem::nextFrame() {
    for (auto &[key, state] : m_keys)         state.previous = state.current;
    for (auto &[btn, state] : m_mouseButtons) state.previous = state.current;
    m_mouseDelta = {0.0f, 0.0f};
}

} // namespace sonnet::input
