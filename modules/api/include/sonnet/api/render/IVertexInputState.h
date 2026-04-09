#pragma once

namespace sonnet::api::render {

class IVertexInputState {
public:
    virtual ~IVertexInputState() = default;
    virtual void bind() const   = 0;
    virtual void unbind() const = 0;
};

} // namespace sonnet::api::render
