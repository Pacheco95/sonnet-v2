#pragma once

#include <array>
#include <optional>
#include <type_traits>

namespace sonnet::api::input {

enum class Key {
    Unknown,
    Space, Apostrophe, Comma, Minus, Period, Slash,
    Zero, One, Two, Three, Four, Five, Six, Seven, Eight, Nine,
    Semicolon, Equal,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket, Backslash, RightBracket, GraveAccent, World1, World2,
    Escape, Enter, Tab, Backspace, Insert, Delete,
    Right, Left, Down, Up,
    PageUp, PageDown, Home, End,
    CapsLock, ScrollLock, NumLock, PrintScreen, Pause,
    F1,  F2,  F3,  F4,  F5,  F6,  F7,  F8,  F9,  F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    NumDecimal, NumDivide, NumMultiply, NumSubtract, NumAdd, NumEnter, NumEqual,
    LeftShift, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper,
    Menu,
    Last,
};

// Compile-time lookup map from an integral platform key code to engine Key.
template <typename MapKey>
class KeyMap {
    static_assert(std::is_integral_v<MapKey>);
    static constexpr auto Capacity = static_cast<std::size_t>(Key::Last);

    struct Entry { MapKey key{}; Key value{}; bool occupied = false; };
    std::array<Entry, Capacity> m_table{};
    std::size_t m_count = 0;

    static constexpr std::size_t hash(MapKey k) {
        return static_cast<std::size_t>(k) % Capacity;
    }

public:
    constexpr bool insert(MapKey k, Key v) {
        if (m_count >= Capacity) return false;
        std::size_t idx = hash(k);
        while (m_table[idx].occupied) {
            if (m_table[idx].key == k) { m_table[idx].value = v; return true; }
            idx = (idx + 1) % Capacity;
        }
        m_table[idx] = {k, v, true};
        ++m_count;
        return true;
    }

    [[nodiscard]] constexpr std::size_t size() const { return m_count; }

    [[nodiscard]] constexpr std::optional<Key> get(MapKey k) const {
        std::size_t idx = hash(k);
        const std::size_t start = idx;
        while (m_table[idx].occupied) {
            if (m_table[idx].key == k) return m_table[idx].value;
            idx = (idx + 1) % Capacity;
            if (idx == start) break;
        }
        return std::nullopt;
    }
};

} // namespace sonnet::api::input
