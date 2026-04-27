#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include "VkBindState.h"
#include "VkDevice.h"
#include "VkFormatMap.h"
#include "VkSamplerCache.h"
#include "VkUtils.h"

#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
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
                         const api::render::SamplerDesc &sampler,
                         const api::render::CubeMapFaces &faces)
    : m_device(device), m_bindState(bindState), m_desc(desc), m_sampler(sampler) {
    if (desc.type != api::render::TextureType::CubeMap) {
        throw VulkanError("VkTexture2D(cubemap ctor): desc.type must be CubeMap");
    }
    const core::Texels *facePtrs[6] = {
        &faces.right, &faces.left, &faces.top, &faces.bottom, &faces.front, &faces.back,
    };
    const std::size_t faceBytes = facePtrs[0]->size();
    if (faceBytes == 0) throw VulkanError("VkTexture2D(cubemap): face 0 is empty");
    for (int i = 1; i < 6; ++i) {
        if (facePtrs[i]->size() != faceBytes) {
            throw VulkanError("VkTexture2D(cubemap): face byte counts differ");
        }
    }

    m_format     = toVkFormat(desc.format, desc.colorSpace);
    m_isDepth    = false;
    m_mipLevels  = mipLevelCount(desc);
    m_layerCount = 6;

    VkImageUsageFlags usage = toVkImageUsage(desc.usageFlags)
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    // Cubemap creation requires CUBE_COMPATIBLE_BIT plus arrayLayers=6.
    info.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = m_format;
    info.extent        = {desc.size.x, desc.size.y, 1};
    info.mipLevels     = m_mipLevels;
    info.arrayLayers   = 6;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = usage;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(device.allocator(), &info, &allocInfo, &m_image, &m_alloc, nullptr));

    // One staging buffer holding all six faces back-to-back. Each face's mip 0
    // gets one VkBufferImageCopy region keyed off baseArrayLayer.
    const VkDeviceSize totalBytes = static_cast<VkDeviceSize>(faceBytes) * 6;
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = totalBytes;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bufAlloc{};
    bufAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    bufAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                   | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer          staging      = VK_NULL_HANDLE;
    VmaAllocation     stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo{};
    VK_CHECK(vmaCreateBuffer(device.allocator(), &bufInfo, &bufAlloc,
                             &staging, &stagingAlloc, &stagingInfo));
    auto *stagingBytes = static_cast<std::uint8_t *>(stagingInfo.pMappedData);
    for (int i = 0; i < 6; ++i) {
        std::memcpy(stagingBytes + i * faceBytes, facePtrs[i]->data(), faceBytes);
    }

    device.runOneShot([&](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, m_image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              m_mipLevels, m_layerCount);

        std::array<VkBufferImageCopy, 6> regions{};
        for (std::uint32_t f = 0; f < 6; ++f) {
            regions[f].bufferOffset                    = f * faceBytes;
            regions[f].bufferRowLength                 = 0;
            regions[f].bufferImageHeight               = 0;
            regions[f].imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[f].imageSubresource.mipLevel       = 0;
            regions[f].imageSubresource.baseArrayLayer = f;
            regions[f].imageSubresource.layerCount     = 1;
            regions[f].imageOffset                     = {0, 0, 0};
            regions[f].imageExtent                     = {desc.size.x, desc.size.y, 1};
        }
        vkCmdCopyBufferToImage(cmd, staging, m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(regions.size()), regions.data());

        if (m_mipLevels > 1) {
            generateMipmaps(cmd);
        } else {
            transitionImageLayout(cmd, m_image,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_ASPECT_COLOR_BIT, m_mipLevels, m_layerCount);
        }
    });

    vmaDestroyBuffer(device.allocator(), staging, stagingAlloc);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format   = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = m_mipLevels;
    viewInfo.subresourceRange.layerCount = 6;
    VK_CHECK(vkCreateImageView(device.logical(), &viewInfo, nullptr, &m_view));

    m_vkSampler = samplers.get(sampler);
}

VkTexture2D::VkTexture2D(Device &device, SamplerCache &samplers, BindState &bindState,
                         const api::render::TextureDesc &desc,
                         const api::render::SamplerDesc &sampler)
    : m_device(device), m_bindState(bindState), m_desc(desc), m_sampler(sampler) {
    const bool isCube = (desc.type == api::render::TextureType::CubeMap);

    m_format     = toVkFormat(desc.format, desc.colorSpace);
    m_isDepth    = isDepthFormat(desc.format);
    m_mipLevels  = 1; // Render targets don't auto-generate mipmaps; per-mip RTs are explicit.
    m_layerCount = isCube ? 6u : 1u;

    VkImageUsageFlags usage = toVkImageUsage(desc.usageFlags)
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // allow readbacks

    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags         = isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = m_format;
    info.extent        = {desc.size.x, desc.size.y, 1};
    info.mipLevels     = m_mipLevels;
    info.arrayLayers   = m_layerCount;
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
                              aspect, 1, m_layerCount);
    });

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = m_image;
    viewInfo.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = m_format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = m_layerCount;
    VK_CHECK(vkCreateImageView(device.logical(), &viewInfo, nullptr, &m_view));

    m_vkSampler = samplers.get(sampler);
}

VkTexture2D::~VkTexture2D() {
    // Free the ImGui descriptor first — its parent pool was created with
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT for exactly this. Skip
    // when ImGui hasn't been initialized (e.g. headless tests).
    if (m_imguiDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_imguiDescriptor);
    }
    if (m_view  != VK_NULL_HANDLE) vkDestroyImageView(m_device.logical(), m_view, nullptr);
    if (m_image != VK_NULL_HANDLE) vmaDestroyImage(m_device.allocator(), m_image, m_alloc);
}

std::uintptr_t VkTexture2D::getImGuiTextureId() {
    if (m_imguiDescriptor == VK_NULL_HANDLE) {
        // ImGui_ImplVulkan_AddTexture allocates from the descriptor pool that
        // was passed to ImGui_ImplVulkan_Init (created with FREE_DESCRIPTOR_SET
        // in VkRendererBackend::initialize). Layout is always
        // SHADER_READ_ONLY_OPTIMAL between passes — same as the layout we
        // transition color targets back to at end-of-pass.
        m_imguiDescriptor = ImGui_ImplVulkan_AddTexture(
            m_vkSampler, m_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return reinterpret_cast<std::uintptr_t>(m_imguiDescriptor);
}

void VkTexture2D::bind(std::uint8_t slot) const {
    // Stage in the slot-keyed table. setUniform(loc, Sampler{slot}) for the
    // matching MaterialSampler entry will fold this into materialTextures[]
    // at the descriptor binding (+ array element) the shader actually expects.
    if (slot < BindState::kMaxMaterialTextures) {
        m_bindState.texturesBySlot[slot] = this;
    }
}

void VkTexture2D::unbind(std::uint8_t slot) const {
    if (slot < BindState::kMaxMaterialTextures &&
        m_bindState.texturesBySlot[slot] == this) {
        m_bindState.texturesBySlot[slot] = nullptr;
    }
}

void VkTexture2D::generateMipmaps(VkCommandBuffer cmd) {
    std::int32_t mipW = static_cast<std::int32_t>(m_desc.size.x);
    std::int32_t mipH = static_cast<std::int32_t>(m_desc.size.y);

    for (std::uint32_t i = 1; i < m_mipLevels; ++i) {
        // Transition previous level (all layers) to TRANSFER_SRC for the blit.
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = m_image;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = i - 1;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.layerCount     = m_layerCount;
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
        blit.srcSubresource.layerCount = m_layerCount;
        blit.srcOffsets[0]             = {0, 0, 0};
        blit.srcOffsets[1]             = {mipW, mipH, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel   = i;
        blit.dstSubresource.layerCount = m_layerCount;
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

    // Final level transition (all layers).
    VkImageMemoryBarrier tail{};
    tail.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tail.image                           = m_image;
    tail.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    tail.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    tail.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    tail.subresourceRange.baseMipLevel   = m_mipLevels - 1;
    tail.subresourceRange.levelCount     = 1;
    tail.subresourceRange.layerCount     = m_layerCount;
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
