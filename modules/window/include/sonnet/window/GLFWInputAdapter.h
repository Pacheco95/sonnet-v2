#pragma once

#include <sonnet/api/input/IInputSink.h>
#include <sonnet/api/input/Key.h>
#include <sonnet/api/input/MouseButton.h>

#include <GLFW/glfw3.h>

#include <unordered_map>

namespace sonnet::window {

using Key         = api::input::Key;
using MouseButton = api::input::MouseButton;

// Compile-time GLFW keycode → engine Key lookup table.
inline constexpr auto GLFW_KEY_MAP = [] {
    api::input::KeyMap<int> map;

    map.insert(GLFW_KEY_UNKNOWN,        Key::Unknown);
    map.insert(GLFW_KEY_SPACE,          Key::Space);
    map.insert(GLFW_KEY_APOSTROPHE,     Key::Apostrophe);
    map.insert(GLFW_KEY_COMMA,          Key::Comma);
    map.insert(GLFW_KEY_MINUS,          Key::Minus);
    map.insert(GLFW_KEY_PERIOD,         Key::Period);
    map.insert(GLFW_KEY_SLASH,          Key::Slash);
    map.insert(GLFW_KEY_0,              Key::Zero);
    map.insert(GLFW_KEY_1,              Key::One);
    map.insert(GLFW_KEY_2,              Key::Two);
    map.insert(GLFW_KEY_3,              Key::Three);
    map.insert(GLFW_KEY_4,              Key::Four);
    map.insert(GLFW_KEY_5,              Key::Five);
    map.insert(GLFW_KEY_6,              Key::Six);
    map.insert(GLFW_KEY_7,              Key::Seven);
    map.insert(GLFW_KEY_8,              Key::Eight);
    map.insert(GLFW_KEY_9,              Key::Nine);
    map.insert(GLFW_KEY_SEMICOLON,      Key::Semicolon);
    map.insert(GLFW_KEY_EQUAL,          Key::Equal);
    map.insert(GLFW_KEY_A,              Key::A);
    map.insert(GLFW_KEY_B,              Key::B);
    map.insert(GLFW_KEY_C,              Key::C);
    map.insert(GLFW_KEY_D,              Key::D);
    map.insert(GLFW_KEY_E,              Key::E);
    map.insert(GLFW_KEY_F,              Key::F);
    map.insert(GLFW_KEY_G,              Key::G);
    map.insert(GLFW_KEY_H,              Key::H);
    map.insert(GLFW_KEY_I,              Key::I);
    map.insert(GLFW_KEY_J,              Key::J);
    map.insert(GLFW_KEY_K,              Key::K);
    map.insert(GLFW_KEY_L,              Key::L);
    map.insert(GLFW_KEY_M,              Key::M);
    map.insert(GLFW_KEY_N,              Key::N);
    map.insert(GLFW_KEY_O,              Key::O);
    map.insert(GLFW_KEY_P,              Key::P);
    map.insert(GLFW_KEY_Q,              Key::Q);
    map.insert(GLFW_KEY_R,              Key::R);
    map.insert(GLFW_KEY_S,              Key::S);
    map.insert(GLFW_KEY_T,              Key::T);
    map.insert(GLFW_KEY_U,              Key::U);
    map.insert(GLFW_KEY_V,              Key::V);
    map.insert(GLFW_KEY_W,              Key::W);
    map.insert(GLFW_KEY_X,              Key::X);
    map.insert(GLFW_KEY_Y,              Key::Y);
    map.insert(GLFW_KEY_Z,              Key::Z);
    map.insert(GLFW_KEY_LEFT_BRACKET,   Key::LeftBracket);
    map.insert(GLFW_KEY_BACKSLASH,      Key::Backslash);
    map.insert(GLFW_KEY_RIGHT_BRACKET,  Key::RightBracket);
    map.insert(GLFW_KEY_GRAVE_ACCENT,   Key::GraveAccent);
    map.insert(GLFW_KEY_WORLD_1,        Key::World1);
    map.insert(GLFW_KEY_WORLD_2,        Key::World2);
    map.insert(GLFW_KEY_ESCAPE,         Key::Escape);
    map.insert(GLFW_KEY_ENTER,          Key::Enter);
    map.insert(GLFW_KEY_TAB,            Key::Tab);
    map.insert(GLFW_KEY_BACKSPACE,      Key::Backspace);
    map.insert(GLFW_KEY_INSERT,         Key::Insert);
    map.insert(GLFW_KEY_DELETE,         Key::Delete);
    map.insert(GLFW_KEY_RIGHT,          Key::Right);
    map.insert(GLFW_KEY_LEFT,           Key::Left);
    map.insert(GLFW_KEY_DOWN,           Key::Down);
    map.insert(GLFW_KEY_UP,             Key::Up);
    map.insert(GLFW_KEY_PAGE_UP,        Key::PageUp);
    map.insert(GLFW_KEY_PAGE_DOWN,      Key::PageDown);
    map.insert(GLFW_KEY_HOME,           Key::Home);
    map.insert(GLFW_KEY_END,            Key::End);
    map.insert(GLFW_KEY_CAPS_LOCK,      Key::CapsLock);
    map.insert(GLFW_KEY_SCROLL_LOCK,    Key::ScrollLock);
    map.insert(GLFW_KEY_NUM_LOCK,       Key::NumLock);
    map.insert(GLFW_KEY_PRINT_SCREEN,   Key::PrintScreen);
    map.insert(GLFW_KEY_PAUSE,          Key::Pause);
    map.insert(GLFW_KEY_F1,             Key::F1);
    map.insert(GLFW_KEY_F2,             Key::F2);
    map.insert(GLFW_KEY_F3,             Key::F3);
    map.insert(GLFW_KEY_F4,             Key::F4);
    map.insert(GLFW_KEY_F5,             Key::F5);
    map.insert(GLFW_KEY_F6,             Key::F6);
    map.insert(GLFW_KEY_F7,             Key::F7);
    map.insert(GLFW_KEY_F8,             Key::F8);
    map.insert(GLFW_KEY_F9,             Key::F9);
    map.insert(GLFW_KEY_F10,            Key::F10);
    map.insert(GLFW_KEY_F11,            Key::F11);
    map.insert(GLFW_KEY_F12,            Key::F12);
    map.insert(GLFW_KEY_F13,            Key::F13);
    map.insert(GLFW_KEY_F14,            Key::F14);
    map.insert(GLFW_KEY_F15,            Key::F15);
    map.insert(GLFW_KEY_F16,            Key::F16);
    map.insert(GLFW_KEY_F17,            Key::F17);
    map.insert(GLFW_KEY_F18,            Key::F18);
    map.insert(GLFW_KEY_F19,            Key::F19);
    map.insert(GLFW_KEY_F20,            Key::F20);
    map.insert(GLFW_KEY_F21,            Key::F21);
    map.insert(GLFW_KEY_F22,            Key::F22);
    map.insert(GLFW_KEY_F23,            Key::F23);
    map.insert(GLFW_KEY_F24,            Key::F24);
    map.insert(GLFW_KEY_F25,            Key::F25);
    map.insert(GLFW_KEY_KP_0,           Key::Num0);
    map.insert(GLFW_KEY_KP_1,           Key::Num1);
    map.insert(GLFW_KEY_KP_2,           Key::Num2);
    map.insert(GLFW_KEY_KP_3,           Key::Num3);
    map.insert(GLFW_KEY_KP_4,           Key::Num4);
    map.insert(GLFW_KEY_KP_5,           Key::Num5);
    map.insert(GLFW_KEY_KP_6,           Key::Num6);
    map.insert(GLFW_KEY_KP_7,           Key::Num7);
    map.insert(GLFW_KEY_KP_8,           Key::Num8);
    map.insert(GLFW_KEY_KP_9,           Key::Num9);
    map.insert(GLFW_KEY_KP_DECIMAL,     Key::NumDecimal);
    map.insert(GLFW_KEY_KP_DIVIDE,      Key::NumDivide);
    map.insert(GLFW_KEY_KP_MULTIPLY,    Key::NumMultiply);
    map.insert(GLFW_KEY_KP_SUBTRACT,    Key::NumSubtract);
    map.insert(GLFW_KEY_KP_ADD,         Key::NumAdd);
    map.insert(GLFW_KEY_KP_ENTER,       Key::NumEnter);
    map.insert(GLFW_KEY_KP_EQUAL,       Key::NumEqual);
    map.insert(GLFW_KEY_LEFT_SHIFT,     Key::LeftShift);
    map.insert(GLFW_KEY_LEFT_CONTROL,   Key::LeftControl);
    map.insert(GLFW_KEY_LEFT_ALT,       Key::LeftAlt);
    map.insert(GLFW_KEY_LEFT_SUPER,     Key::LeftSuper);
    map.insert(GLFW_KEY_RIGHT_SHIFT,    Key::RightShift);
    map.insert(GLFW_KEY_RIGHT_CONTROL,  Key::RightControl);
    map.insert(GLFW_KEY_RIGHT_ALT,      Key::RightAlt);
    map.insert(GLFW_KEY_RIGHT_SUPER,    Key::RightSuper);
    map.insert(GLFW_KEY_MENU,           Key::Menu);

    return map;
}();

inline const std::unordered_map<int, MouseButton> GLFW_MOUSE_BUTTON_MAP = {
    {GLFW_MOUSE_BUTTON_LEFT,   MouseButton::Left},
    {GLFW_MOUSE_BUTTON_MIDDLE, MouseButton::Middle},
    {GLFW_MOUSE_BUTTON_RIGHT,  MouseButton::Right},
};

class GLFWInputAdapter {
public:
    explicit GLFWInputAdapter(api::input::IInputSink &sink) : m_sink(sink) {}

    void onKey(GLFWwindow *, int glfwKey, int /*scanCode*/, int action, int /*mods*/) const;
    void onMouseButton(GLFWwindow *, int button, int action, int /*mods*/) const;
    void onMouseMove(GLFWwindow *, double x, double y) const;

private:
    api::input::IInputSink &m_sink;
};

} // namespace sonnet::window
