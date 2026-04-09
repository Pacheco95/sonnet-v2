#pragma once

#include <sonnet/api/input/Key.h>
#include <sonnet/api/input/MouseButton.h>

#include <glm/glm.hpp>

namespace sonnet::api::input {

class IInput {
public:
    virtual ~IInput() = default;

    [[nodiscard]] virtual bool isKeyDown(Key key) const = 0;
    [[nodiscard]] virtual bool isKeyJustPressed(Key key) const = 0;
    [[nodiscard]] virtual bool isKeyJustReleased(Key key) const = 0;

    [[nodiscard]] virtual bool isMouseDown(MouseButton button) const = 0;
    [[nodiscard]] virtual bool isMouseJustPressed(MouseButton button) const = 0;
    [[nodiscard]] virtual bool isMouseJustReleased(MouseButton button) const = 0;

    [[nodiscard]] virtual glm::vec2 mouseDelta() const = 0;

    // Advance to the next frame — resets JustPressed / JustReleased states.
    virtual void nextFrame() = 0;
};

} // namespace sonnet::api::input
