#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include "VkBindState.h"
#include "VkDevice.h"
#include "VkFormatMap.h"
#include "VkSamplerCache.h"
#include "VkUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sonnet::renderer::vulkan {

namespace {

std::uint32_t mipLevelCount(const api::render::TextureDesc &desc) {
    if (!desc.useMipmaps) return 1;
    if (desc.type != api::render::TextureType::Texture2D) return 1;
    const std::uint32_t maxDim = std::max(desc.size.x, desc.size.y);
    return static_cast<std::uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkImageAspectFlags aspect,
                           std::uint32_t mipLevels,
                           std::uint32_t layerCount) {
    VkImageMemoryBarrier barrier{};
    barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                   = oldLayout;
    barrier.newLayout                   = newLayout;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                              | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw VulkanError("transitionImageLayout: unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

VkTexture2D::VkTexture2D(Device &device, SamplerCache &samplers, BindState &bindState,
                         const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler,
                         const api::render::CPUTextureBuffer &data)
    : m_device(device), m_bindState(bindState), m_desc(desc), m_sampler(sampler) {
    if (desc.type != api::render::TextureType::Texture2D) {
        throw VulkanError("VkTexture2D: CubeMap upload path not implemented (Phase 7 / IBL)");
    }
    if (data.texels.empty()) {
        throw VulkanError("VkTexture2D: empty texel data");
    }

    m_format    = toVkFormat(desc.format, desc.colorSpace);
    m_isDepth   = isDepthFormat(desc.format);
    m_mipLevels = mipLevelCount(desc);

    // Always add TRANSFER_DST + TRANSFER_SRC (needed for staging upload and mipmap blits).
    VkImageUsageFlags usage = toVkImageUsage(desc.usageFlags)
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = m_format;
    info.extent        = {desc.size.x, desc.size.y, 1};
    info.mipLevels     = m_mipLevels;
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = usage;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(device.allocator(), &info, &allocInfo, &m_image, &m_alloc, nullptr));

    // Staging buffer with the CPU data.
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(data.texels.size());
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bufAlloc{};
    bufAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    bufAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                   | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer          staging     = VK_NULL_HANDLE;
    VmaAllocation     stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo{};
    VK_CHECK(vmaCreateBuffer(device.allocator(), &bufInfo, &bufAlloc,
                             &staging, &stagingAlloc, &stagingInfo));
    std::memcpy(stagingInfo.pMappedData, data.texels.data(), byteSize);

    device.runOneShot([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, m_image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              m_mipLevels, 1);

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = {desc.size.x, desc.size.y, 1};
        vkCmdCopyBufferToImage(cmd, staging, m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        if (m_mipLevels > 1) {
            generateMipmaps(cmd);
        } else {
            transitionImageLayout(cmd, m_image,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_ASPECT_COLOR_BIT, m_mipLevels, 1);
        }
    });

    vmaDestroyBuffer(device.allocator(), staging, stagingAlloc);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = m_format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount     = m_mipLevels;
    viewInfo.subresourceRange.layerCount     = 1;
    VK_CHECK(vkCreateImageView(device.logical(), &viewInfo, nullptr, &m_view));

    m_vkSampler = samplers.get(sampler);
}

VkTexture2D::VkTexture2D(Device &device, SamplerCache &samplers, BindState &bindState,
                         const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler)
    : m_device(device), m_bindState(bindState), m_desc(desc), m_sampler(sampler) {
    if (desc.type != api::render::TextureType::Texture2D) {
        throw VulkanError("VkTexture2D(allocate-only): CubeMap path not implemented");
    }

    m_format    = toVkFormat(desc.format, desc.colorSpace);
    m_isDepth   = isDepthFormat(desc.format);
    m_mipLevels = 1; // Render targets don't use mipmaps.

    VkImageUsageFlags usage = toVkImageUsage(desc.usageFlags)
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // allow readbacks

    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = m_format;
    info.extent        = {desc.size.x, desc.size.y, 1};
    info.mipLevels     = m_mipLevels;
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = usage;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(device.allocator(), &info, &allocInfo, &m_image, &m_alloc, nullptr));

    const VkImageAspectFlags aspect = m_isDepth
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

    // Transition to SHADER_READ_ONLY for color, DEPTH_STENCIL_ATTACHMENT for depth.
    // Render-pass begin will transition back to the attachment-optimal layout.
    const VkImageLayout initialLayout = m_isDepth
        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    device.runOneShot([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, m_image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              initialLayout,
                              aspect, 1, 1);
    });

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = m_format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device.logical(), &viewInfo, nullptr, &m_view));

    m_vkSampler = samplers.get(sampler);
}

VkTexture2D::~VkTexture2D() {
    if (m_view  != VK_NULL_HANDLE) vkDestroyImageView(m_device.logical(), m_view, nullptr);
    if (m_image != VK_NULL_HANDLE) vmaDestroyImage(m_device.allocator(), m_image, m_alloc);
}

void VkTexture2D::bind(std::uint8_t slot) const {
    // The slot value Renderer::bindMaterial iterates (0, 1, 2, ...) matches
    // the descriptor binding in set=1. Record it for drawIndexed to consume
    // when building the material descriptor set.
    if (slot < BindState::kMaxMaterialTextures) {
        m_bindState.materialTextures[slot] = this;
    }
}

void VkTexture2D::unbind(std::uint8_t slot) const {
    if (slot < BindState::kMaxMaterialTextures &&
        m_bindState.materialTextures[slot] == this) {
        m_bindState.materialTextures[slot] = nullptr;
    }
}

void VkTexture2D::generateMipmaps(VkCommandBuffer cmd) {
    std::int32_t mipW = static_cast<std::int32_t>(m_desc.size.x);
    std::int32_t mipH = static_cast<std::int32_t>(m_desc.size.y);

    for (std::uint32_t i = 1; i < m_mipLevels; ++i) {
        // Transition previous level to TRANSFER_SRC for the blit.
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = m_image;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = i - 1;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.layerCount     = 1;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel   = i - 1;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0]             = {0, 0, 0};
        blit.srcOffsets[1]             = {mipW, mipH, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel   = i;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0]             = {0, 0, 0};
        blit.dstOffsets[1]             = {mipW > 1 ? mipW / 2 : 1,
                                          mipH > 1 ? mipH / 2 : 1, 1};
        vkCmdBlitImage(cmd,
                       m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        // Transition i-1 to SHADER_READ_ONLY now that its contents are final.
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        if (mipW > 1) mipW /= 2;
        if (mipH > 1) mipH /= 2;
    }

    // Final level transition.
    VkImageMemoryBarrier tail{};
    tail.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tail.image                           = m_image;
    tail.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    tail.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    tail.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    tail.subresourceRange.baseMipLevel   = m_mipLevels - 1;
    tail.subresourceRange.levelCount     = 1;
    tail.subresourceRange.layerCount     = 1;
    tail.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tail.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tail.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    tail.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &tail);
}

} // namespace sonnet::renderer::vulkan
