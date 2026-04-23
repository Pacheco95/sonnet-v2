#pragma once

#include <sonnet/api/render/ITexture.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace sonnet::api::render {

struct TextureAttachmentDesc {
    TextureFormat format;
    SamplerDesc   samplerDesc;
};

struct RenderBufferDesc {};

using DepthAttachmentDesc = std::variant<TextureAttachmentDesc, RenderBufferDesc>;

struct RenderTargetDesc {
    std::uint32_t                        width;
    std::uint32_t                        height;
    std::vector<TextureAttachmentDesc>   colors;
    std::optional<DepthAttachmentDesc>   depth;
};

class IRenderTarget {
public:
    virtual ~IRenderTarget() = default;

    [[nodiscard]] virtual std::uint32_t  width()  const = 0;
    [[nodiscard]] virtual std::uint32_t  height() const = 0;
    virtual void                         bind()   const = 0;

    [[nodiscard]] virtual const ITexture *colorTexture(std::size_t index) const = 0;
    [[nodiscard]] virtual const ITexture *depthTexture()                  const = 0;

    // Read one RGBA8 pixel from the given color attachment at (x, y). The
    // origin is the bottom-left corner of the attachment (matching OpenGL
    // conventions so existing picking code doesn't have to flip). Blocks
    // until the read completes — used by user-driven picking / debug.
    // Throws if the attachment index is out of range, or the format isn't
    // convertible to RGBA8 on the backend.
    [[nodiscard]] virtual std::array<std::uint8_t, 4> readPixelRGBA8(
        std::uint32_t attachmentIndex,
        std::uint32_t x, std::uint32_t y) const = 0;
};

} // namespace sonnet::api::render
