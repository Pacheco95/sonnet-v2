#pragma once

#include <sonnet/core/Hash.h>
#include <sonnet/core/Types.h>

#include <cstdint>
#include <glm/glm.hpp>

namespace sonnet::api::render {

enum class TextureFormat  : std::uint8_t { RGB8, RGBA8, RGBA16F, RGBA32F, R32F, Depth24 };
enum class TextureWrap    : std::uint8_t { Repeat, ClampToEdge };
enum class TextureType    : std::uint8_t { Texture2D, CubeMap };
enum class MagFilter      : std::uint8_t { Nearest, Linear };
enum class MinFilter      : std::uint8_t {
    Nearest, Linear,
    NearestMipmapNearest, LinearMipmapNearest,
    NearestMipmapLinear,  LinearMipmapLinear
};
enum class ColorSpace     : std::uint8_t { Linear, sRGB };
enum TextureUsage         : std::uint8_t {
    None            = 0,
    Sampled         = 1 << 0,
    ColorAttachment = 1 << 1,
    DepthAttachment = 1 << 2,
};

struct TextureDesc {
    glm::uvec2   size       = {1, 1};
    TextureFormat format    = TextureFormat::RGBA8;
    TextureType   type      = TextureType::Texture2D;
    TextureUsage  usageFlags= TextureUsage::Sampled;
    ColorSpace    colorSpace= ColorSpace::Linear;
    bool          useMipmaps= true;

    bool operator==(const TextureDesc &) const = default;
};

struct SamplerDesc {
    MinFilter minFilter = MinFilter::LinearMipmapLinear;
    MagFilter magFilter = MagFilter::Linear;
    TextureWrap wrapS   = TextureWrap::Repeat;
    TextureWrap wrapT   = TextureWrap::Repeat;
    TextureWrap wrapR   = TextureWrap::Repeat;
    // When true, the texture is a shadow sampler (sampler2DShadow).
    // The backend sets GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE
    // and GL_TEXTURE_COMPARE_FUNC = GL_LEQUAL so that texture() returns a
    // hardware-filtered [0,1] shadow factor rather than a raw depth value.
    bool depthCompare   = false;

    bool operator==(const SamplerDesc &) const = default;

    [[nodiscard]] constexpr bool requiresMipmaps() const noexcept {
        return minFilter != MinFilter::Nearest && minFilter != MinFilter::Linear;
    }
};

struct CPUTextureBuffer {
    std::uint32_t  width;
    std::uint32_t  height;
    std::uint32_t  channels;
    core::Texels   texels;
};

struct CubeMapFaces {
    const core::Texels &right, &left, &top, &bottom, &front, &back;
};

class ITexture {
public:
    virtual ~ITexture() = default;

    virtual void bind(std::uint8_t slot) const   = 0;
    virtual void unbind(std::uint8_t slot) const = 0;

    [[nodiscard]] virtual const TextureDesc  &textureDesc() const = 0;
    [[nodiscard]] virtual const SamplerDesc  &samplerDesc() const = 0;
    [[nodiscard]] virtual unsigned            getNativeHandle() const = 0;

    // ImGui-facing texture identifier. Callers cast to ImTextureID.
    // GL backend returns the GLuint widened to uintptr_t. Vulkan backend
    // returns a VkDescriptorSet (lazily allocated via ImGui_ImplVulkan_AddTexture
    // and cached). Non-const because the Vulkan lazy-cache mutates state.
    [[nodiscard]] virtual std::uintptr_t      getImGuiTextureId() = 0;
};

} // namespace sonnet::api::render
