#pragma once

#include <sonnet/api/input/Key.h>
#include <sonnet/api/input/MouseButton.h>

#include <functional>

template <>
struct std::hash<sonnet::api::input::Key> {
    std::size_t operator()(sonnet::api::input::Key k) const noexcept {
        return std::hash<int>{}(static_cast<int>(k));
    }
};

template <>
struct std::hash<sonnet::api::input::MouseButton> {
    std::size_t operator()(sonnet::api::input::MouseButton b) const noexcept {
        return std::hash<int>{}(static_cast<int>(b));
    }
};
