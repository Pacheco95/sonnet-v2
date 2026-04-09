#pragma once

#include <sonnet/api/input/Key.h>

namespace sonnet::api::input {

struct KeyEvent {
    Key  key;
    bool pressed;
};

} // namespace sonnet::api::input
