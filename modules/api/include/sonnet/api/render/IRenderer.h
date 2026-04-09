#pragma once

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/RenderItem.h>
#include <sonnet/api/render/RenderState.h>

#include <vector>

namespace sonnet::api::render {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void beginFrame() = 0;
    virtual void render(const FrameContext &ctx, std::vector<RenderItem> &queue) = 0;
    virtual void endFrame()   = 0;

    // Optional per-frame overrides (pass nullptr to clear).
    virtual void setOverrides(RenderOverrides *overrides) = 0;
};

} // namespace sonnet::api::render
