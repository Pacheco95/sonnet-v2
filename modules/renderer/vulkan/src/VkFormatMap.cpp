#include "VkFormatMap.h"

#include "VkUtils.h"

namespace sonnet::renderer::vulkan {

VkFormat toVkFormat(api::render::TextureFormat fmt, api::render::ColorSpace colorSpace) {
    using F = api::render::TextureFormat;
    using C = api::render::ColorSpace;
    const bool srgb = (colorSpace == C::sRGB);
    switch (fmt) {
        case F::RGB8:    return srgb ? VK_FORMAT_R8G8B8_SRGB   : VK_FORMAT_R8G8B8_UNORM;
        case F::RGBA8:   return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        case F::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case F::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case F::R32F:    return VK_FORMAT_R32_SFLOAT;
        case F::RG16F:   return VK_FORMAT_R16G16_SFLOAT;
        // Vulkan core lacks RGB16F as a sampled-image format (it's optional).
        // Most consumer GPUs widen 3-channel to 4 anyway, so map RGB16F to
        // RGBA16F. The alpha channel is wasted but storage-cost neutral.
        case F::RGB16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case F::Depth24: return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    throw VulkanError("toVkFormat: unknown TextureFormat");
}

bool isDepthFormat(api::render::TextureFormat fmt) {
    return fmt == api::render::TextureFormat::Depth24;
}

VkImageUsageFlags toVkImageUsage(api::render::TextureUsage usage) {
    VkImageUsageFlags flags = 0;
    if (usage & api::render::Sampled)         flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & api::render::ColorAttachment) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage & api::render::DepthAttachment) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return flags;
}

VkSamplerAddressMode toVkAddressMode(api::render::TextureWrap wrap) {
    switch (wrap) {
        case api::render::TextureWrap::Repeat:      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case api::render::TextureWrap::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    throw VulkanError("toVkAddressMode: unknown TextureWrap");
}

VkFilter toVkMagFilter(api::render::MagFilter f) {
    switch (f) {
        case api::render::MagFilter::Nearest: return VK_FILTER_NEAREST;
        case api::render::MagFilter::Linear:  return VK_FILTER_LINEAR;
    }
    throw VulkanError("toVkMagFilter: unknown MagFilter");
}

VkMinFilterParts toVkMinFilter(api::render::MinFilter f) {
    using MF = api::render::MinFilter;
    switch (f) {
        case MF::Nearest:              return {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST};
        case MF::Linear:               return {VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_NEAREST};
        case MF::NearestMipmapNearest: return {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST};
        case MF::LinearMipmapNearest:  return {VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_NEAREST};
        case MF::NearestMipmapLinear:  return {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR};
        case MF::LinearMipmapLinear:   return {VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_LINEAR};
    }
    throw VulkanError("toVkMinFilter: unknown MinFilter");
}

std::uint32_t bytesPerPixel(api::render::TextureFormat fmt) {
    using F = api::render::TextureFormat;
    switch (fmt) {
        case F::RGB8:    return 3;
        case F::RGBA8:   return 4;
        case F::RGBA16F: return 8;
        case F::RGBA32F: return 16;
        case F::R32F:    return 4;
        case F::RG16F:   return 4;
        // RGB16F widens to RGBA16F in toVkFormat, so storage cost is 8 bytes.
        case F::RGB16F:  return 8;
        case F::Depth24: return 4; // (D24 stored as 32-bit w/ stencil in most impls)
    }
    throw VulkanError("bytesPerPixel: unknown TextureFormat");
}

} // namespace sonnet::renderer::vulkan
