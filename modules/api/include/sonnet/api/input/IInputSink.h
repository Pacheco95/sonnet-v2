#pragma once

#include <sonnet/api/input/KeyEvent.h>
#include <sonnet/api/input/MouseEvent.h>

namespace sonnet::api::input {

class IInputSink {
public:
    virtual ~IInputSink() = default;
    virtual void onKeyEvent(const KeyEvent &event) = 0;
    virtual void onMouseEvent(const MouseEvent &event) = 0;
};

} // namespace sonnet::api::input
