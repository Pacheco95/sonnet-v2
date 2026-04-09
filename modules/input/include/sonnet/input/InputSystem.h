#pragma once

#include <sonnet/api/input/IInput.h>
#include <sonnet/api/input/IInputSink.h>
#include <sonnet/input/InputHash.h>

#include <glm/glm.hpp>
#include <unordered_map>

namespace sonnet::input {

class InputSystem final : public api::input::IInput, public api::input::IInputSink {
public:
    // IInputSink
    void onKeyEvent(const api::input::KeyEvent &event) override;
    void onMouseEvent(const api::input::MouseEvent &event) override;

    // IInput
    [[nodiscard]] bool isKeyDown(api::input::Key key)         const override;
    [[nodiscard]] bool isKeyJustPressed(api::input::Key key)  const override;
    [[nodiscard]] bool isKeyJustReleased(api::input::Key key) const override;

    [[nodiscard]] bool isMouseDown(api::input::MouseButton button)         const override;
    [[nodiscard]] bool isMouseJustPressed(api::input::MouseButton button)  const override;
    [[nodiscard]] bool isMouseJustReleased(api::input::MouseButton button) const override;

    [[nodiscard]] glm::vec2 mouseDelta() const override;

    void nextFrame() override;

private:
    struct PressedState { bool current = false; bool previous = false; };

    std::unordered_map<api::input::Key, PressedState>         m_keys;
    std::unordered_map<api::input::MouseButton, PressedState> m_mouseButtons;

    glm::vec2 m_mousePosition{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    bool      m_hasMousePosition{false};
};

} // namespace sonnet::input
