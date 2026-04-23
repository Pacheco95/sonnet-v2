#pragma once

#include <sonnet/api/render/ITexture.h>

#include <vulkan/vulkan.h>

namespace sonnet::renderer::vulkan {

// Engine TextureFormat + ColorSpace → VkFormat. Depth formats ignore ColorSpace.
[[nodiscard]] VkFormat toVkFormat(api::render::TextureFormat fmt,
                                  api::render::ColorSpace     colorSpace);

[[nodiscard]] bool isDepthFormat(api::render::TextureFormat fmt);

// Engine TextureUsage → VkImageUsageFlags. Sampled/ColorAttachment/DepthAttachment
// ORed together as the caller requests.
[[nodiscard]] VkImageUsageFlags toVkImageUsage(api::render::TextureUsage usage);

// Sampler conversions.
[[nodiscard]] VkSamplerAddressMode toVkAddressMode(api::render::TextureWrap wrap);

// Mag filter: Nearest / Linear → VK_FILTER_NEAREST / VK_FILTER_LINEAR.
[[nodiscard]] VkFilter toVkMagFilter(api::render::MagFilter f);

// Min filter: converts the 6-way engine enum into (VkFilter minFilter,
// VkSamplerMipmapMode mipmapMode). The caller applies both to VkSamplerCreateInfo.
struct VkMinFilterParts { VkFilter filter; VkSamplerMipmapMode mipmap; };
[[nodiscard]] VkMinFilterParts toVkMinFilter(api::render::MinFilter f);

// Format → pixel size in bytes (for CPU→GPU staging copy). Throws on formats
// without a straightforward byte-per-pixel mapping.
[[nodiscard]] std::uint32_t bytesPerPixel(api::render::TextureFormat fmt);

} // namespace sonnet::renderer::vulkan
