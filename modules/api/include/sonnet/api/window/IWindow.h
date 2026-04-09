#pragma once

#include <string>
#include <glm/glm.hpp>

namespace sonnet::api::window {

enum class WindowState : std::uint8_t {
    Normal,
    Focused,
    Unfocused,
    Minimized,
    Maximized,
    Fullscreen,
    Hidden,
    Closed,
};

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual void setTitle(const std::string &title) = 0;
    [[nodiscard]] virtual const std::string &getTitle() const = 0;

    [[nodiscard]] virtual glm::uvec2 getFrameBufferSize() const = 0;

    virtual void setVisible(bool visible) = 0;
    [[nodiscard]] virtual bool isVisible() const = 0;

    virtual void requestClose() = 0;
    [[nodiscard]] virtual bool shouldClose() const = 0;

    virtual void pollEvents() = 0;
    virtual void swapBuffers() = 0;

    virtual void toggleFullscreen() = 0;
    virtual void captureCursor() = 0;
    virtual void releaseCursor() = 0;

    [[nodiscard]] virtual WindowState getState() const = 0;
};

} // namespace sonnet::api::window
