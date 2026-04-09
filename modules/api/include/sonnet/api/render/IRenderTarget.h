#pragma once

#include <sonnet/api/render/ITexture.h>

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
};

} // namespace sonnet::api::render
