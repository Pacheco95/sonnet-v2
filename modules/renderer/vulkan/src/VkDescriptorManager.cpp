#include "VkDescriptorManager.h"

#include <sonnet/renderer/vulkan/VkShader.h>
#include <sonnet/renderer/vulkan/VkTexture2D.h>

#include "VkBindState.h"
#include "VkDevice.h"
#include "VkSamplerCache.h"
#include "VkUtils.h"

#include <array>
#include <cstring>
#include <vector>

namespace sonnet::renderer::vulkan {

namespace {

// Per-frame pool capacity. Sized to comfortably cover every draw-time set
// allocation in a frame without needing auto-grow logic in v1. Increase
// if drawIndexed ever returns OUT_OF_POOL_MEMORY.
constexpr std::uint32_t kMaxSetsPerFrame                  = 512;
constexpr std::uint32_t kMaxUniformBuffersPerFrame        = 1024;
constexpr std::uint32_t kMaxCombinedImageSamplersPerFrame = 4096;
constexpr std::uint32_t kMaxSamplersPerFrame              = 256;
constexpr std::uint32_t kMaxSampledImagesPerFrame         = 1024;

} // namespace

DescriptorManager::DescriptorManager(Device &device, SamplerCache &samplerCache, BindState &bindState)
    : m_device(device), m_samplerCache(samplerCache), m_bindState(bindState) {
    const std::array<VkDescriptorPoolSize, 4> sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kMaxUniformBuffersPerFrame},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxCombinedImageSamplersPerFrame},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER,                kMaxSamplersPerFrame},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          kMaxSampledImagesPerFrame},
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets       = kMaxSetsPerFrame;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes    = sizes.data();

    for (auto &pool : m_pools) {
        VK_CHECK(vkCreateDescriptorPool(device.logical(), &info, nullptr, &pool));
    }

    buildDefaultTexture();
}

DescriptorManager::~DescriptorManager() {
    if (m_defaultImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device.logical(), m_defaultImageView, nullptr);
    }
    if (m_defaultImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device.allocator(), m_defaultImage, m_defaultAlloc);
    }
    for (auto &pool : m_pools) {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device.logical(), pool, nullptr);
    }
}

void DescriptorManager::buildDefaultTexture() {
    // 1x1 RGBA8 white texture used as a fallback when a shader's material
    // binding has no texture supplied by the material instance.
    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent        = {1, 1, 1};
    info.mipLevels     = 1;
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &info, &allocInfo,
                             &m_defaultImage, &m_defaultAlloc, nullptr));

    // Upload {255,255,255,255}.
    const std::uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = 4;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bufAlloc{};
    bufAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    bufAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                   | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo{};
    VK_CHECK(vmaCreateBuffer(m_device.allocator(), &bufInfo, &bufAlloc,
                              &staging, &stagingAlloc, &stagingInfo));
    std::memcpy(stagingInfo.pMappedData, white, sizeof(white));

    m_device.runOneShot([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = m_defaultImage;
        toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toDst.subresourceRange.levelCount = 1;
        toDst.subresourceRange.layerCount = 1;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd, staging, m_defaultImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
    });

    vmaDestroyBuffer(m_device.allocator(), staging, stagingAlloc);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_defaultImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(m_device.logical(), &viewInfo, nullptr, &m_defaultImageView));

    api::render::SamplerDesc defaultSamp{};
    defaultSamp.minFilter = api::render::MinFilter::Linear;
    defaultSamp.magFilter = api::render::MagFilter::Linear;
    defaultSamp.wrapS     = api::render::TextureWrap::Repeat;
    defaultSamp.wrapT     = api::render::TextureWrap::Repeat;
    m_defaultSampler = m_samplerCache.get(defaultSamp);
}

void DescriptorManager::beginFrame(std::uint32_t frameIx) {
    m_currentFrame = frameIx % kFramesInFlight;
    vkResetDescriptorPool(m_device.logical(), m_pools[m_currentFrame], 0);
}

VkDescriptorSet DescriptorManager::allocateFrameSet0(const VkShader &shader) {
    const auto &layouts = shader.setLayouts();
    if (layouts.empty()) return VK_NULL_HANDLE;

    const auto &reflection = shader.reflection();
    if (reflection.setBindings.empty() || reflection.setBindings[0].empty()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = m_pools[m_currentFrame];
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &layouts[0];

    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(m_device.logical(), &alloc, &set));

    // Populate each UBO binding from the frame's BindState.ubos[] table.
    // Skip bindings that haven't been bound; Vulkan needs every binding in
    // the layout to be valid at draw time, so if the engine omits a UBO it's
    // the engine's responsibility to ensure the shader doesn't reference it.
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet>   writes;
    bufferInfos.reserve(reflection.setBindings[0].size());
    writes.reserve(reflection.setBindings[0].size());

    for (const auto &b : reflection.setBindings[0]) {
        if (b.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
        if (b.binding >= BindState::kMaxUboBindings) continue;
        if (m_bindState.ubos[b.binding] == VK_NULL_HANDLE)         continue;

        VkDescriptorBufferInfo info{};
        info.buffer = m_bindState.ubos[b.binding];
        info.offset = 0;
        info.range  = m_bindState.uboSizes[b.binding];
        bufferInfos.push_back(info);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = b.binding;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo     = &bufferInfos.back();
        writes.push_back(w);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device.logical(),
                               static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    return set;
}

VkDescriptorSet DescriptorManager::allocateMaterialSet1(const VkShader &shader) {
    const auto &layouts = shader.setLayouts();
    if (layouts.size() < 2) return VK_NULL_HANDLE;

    const auto &reflection = shader.reflection();
    if (reflection.setBindings.size() < 2 || reflection.setBindings[1].empty()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = m_pools[m_currentFrame];
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &layouts[1];

    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(m_device.logical(), &alloc, &set));

    // Each VkWriteDescriptorSet's pImageInfo must remain stable until the
    // vkUpdateDescriptorSets call, so collect every image info into a flat
    // vector (reserved to its final size up front to prevent reallocation
    // invalidating earlier write pointers) and slice into it per binding.
    std::size_t totalImageInfos = 0;
    for (const auto &b : reflection.setBindings[1]) {
        if (b.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            b.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) continue;
        totalImageInfos += b.descriptorCount;
    }

    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet>  writes;
    imageInfos.reserve(totalImageInfos);
    writes.reserve(reflection.setBindings[1].size());

    for (const auto &b : reflection.setBindings[1]) {
        if (b.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            b.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) continue;

        // Every binding (including every array element) must be written.
        // Slots the material instance didn't bind get the fallback 1x1
        // white texture so vkUpdateDescriptorSets always supplies a valid
        // (imageView, sampler) pair and the shader can sample without
        // tripping the descriptor-validity validation error.
        if (b.binding + b.descriptorCount > BindState::kMaxMaterialTextures) continue;

        const auto firstIx = imageInfos.size();
        for (std::uint32_t i = 0; i < b.descriptorCount; ++i) {
            const auto *tex = m_bindState.materialTextures[b.binding + i];
            VkDescriptorImageInfo ii{};
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (tex != nullptr) {
                ii.imageView = tex->imageView();
                ii.sampler   = tex->sampler();
            } else {
                ii.imageView = m_defaultImageView;
                ii.sampler   = m_defaultSampler;
            }
            imageInfos.push_back(ii);
        }

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = b.binding;
        w.dstArrayElement = 0;
        w.descriptorCount = b.descriptorCount;
        w.descriptorType  = b.descriptorType;
        w.pImageInfo      = imageInfos.data() + firstIx;
        writes.push_back(w);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device.logical(),
                               static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    return set;
}

VkDescriptorSet DescriptorManager::allocatePerDrawSet2(const VkShader &shader,
                                                       VkBuffer ringBuffer,
                                                       VkDeviceSize offset,
                                                       VkDeviceSize range) {
    const auto &layouts = shader.setLayouts();
    if (layouts.size() < 3) return VK_NULL_HANDLE;

    const auto &reflection = shader.reflection();
    if (reflection.setBindings.size() < 3 || reflection.setBindings[2].empty()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = m_pools[m_currentFrame];
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &layouts[2];

    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(m_device.logical(), &alloc, &set));

    VkDescriptorBufferInfo bi{};
    bi.buffer = ringBuffer;
    bi.offset = offset;
    bi.range  = range;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo     = &bi;
    vkUpdateDescriptorSets(m_device.logical(), 1, &w, 0, nullptr);

    return set;
}

} // namespace sonnet::renderer::vulkan
