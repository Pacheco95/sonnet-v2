#pragma once

#include <sonnet/api/input/MouseButton.h>

#include <glm/glm.hpp>
#include <variant>

namespace sonnet::api::input {

struct MouseMovedEvent   { glm::vec2 position; };
struct MouseButtonEvent  { MouseButton button; bool pressed; };

using MouseEvent = std::variant<MouseButtonEvent, MouseMovedEvent>;

} // namespace sonnet::api::input
