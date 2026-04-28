#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace sonnet::renderer::vulkan {

class VkShader;
class VkVertexInputState;
class VkTexture2D;

// Mutable state the backend's drawIndexed() reads to assemble a draw:
// bound buffers, active shader, active vertex input, UBO table (set=0),
// material texture table (set=1), and push-constant staging (set by
// setUniform). Owned by VkRendererBackend; passed by reference to the
// objects that mutate it. This is Vulkan's emulation of OpenGL's context
// binding model.
struct BindState {
    static constexpr std::uint32_t kMaxUboBindings      = 8;
    // Sized to comfortably cover the deferred-lighting shader's binding=14
    // (uSSAO) plus headroom; uPointShadowMaps[4] uses bindings 7–10.
    static constexpr std::uint32_t kMaxMaterialTextures = 32;
    static constexpr std::uint32_t kPushConstantBytes   = 128;
    // Large enough for skinned meshes' 128 mat4 bone-matrix array (8 KB) plus
    // per-draw header fields; also covers SSAO's 1 KB kernel with plenty of room.
    static constexpr std::uint32_t kPerDrawStagingBytes = 16384;

    VkBuffer                              currentVertex = VK_NULL_HANDLE;
    VkBuffer                              currentIndex  = VK_NULL_HANDLE;
    VkIndexType                           indexType     = VK_INDEX_TYPE_UINT32;

    // Active per-frame command buffer. Set by VkRendererBackend::beginFrame
    // and cleared in endFrame. VkGpuBuffer::update uses this to record
    // vkCmdUpdateBuffer (serialized with command-buffer execution) instead
    // of a host memcpy — necessary because the engine reuses single
    // CameraUBO/LightsUBO buffers across many passes per frame, and a host
    // memcpy makes every draw read the LAST write at GPU execute time
    // rather than the value that was current when its pass was recorded.
    VkCommandBuffer                       currentCmd    = VK_NULL_HANDLE;

    // Mirror of VkRendererBackend::m_passActive set by ensurePassActive /
    // bindRenderTarget / endFrame. VkGpuBuffer::update consults this to
    // decide whether vkCmdUpdateBuffer is currently legal (only outside a
    // render pass per spec). Inside a pass the update is dropped and the
    // last-written value persists — the demo's only multi-render-per-pass
    // pattern (deferred + sky + outline composite into hdrRT) feeds the
    // same scene camera matrix to each render() call, so dropping the
    // duplicate write is functionally equivalent. A future Phase-8 ring
    // buffer would let updates inside passes target a fresh slot.
    bool                                  passActive    = false;
    std::array<VkBuffer, kMaxUboBindings> ubos{};
    std::array<VkDeviceSize, kMaxUboBindings> uboSizes{};

    const VkShader           *currentShader      = nullptr;
    const VkVertexInputState *currentVertexInput = nullptr;

    // Slot-indexed staging table. VkTexture2D::bind(slot) records the texture
    // here; setUniform(loc, Sampler{slot}) for a MaterialSampler entry then
    // reads texturesBySlot[slot] and writes it to materialTextures[binding +
    // arrayElement]. This decouples the engine's slot iteration order
    // (Renderer::bindMaterial assigns 0..N-1) from descriptor-binding numbers
    // chosen in the shader source.
    std::array<const VkTexture2D *, kMaxMaterialTextures> texturesBySlot{};

    // Material textures, indexed by descriptor binding (or binding +
    // arrayElement for sampler arrays). Read by VkDescriptorManager when
    // building the set=1 descriptor set.
    std::array<const VkTexture2D *, kMaxMaterialTextures> materialTextures{};

    // Staged push-constant bytes + hwm of the last byte written this draw.
    std::array<std::uint8_t, kPushConstantBytes> pushConstantStaging{};
    std::uint32_t                                pushConstantDirtyEnd = 0;

    // Staged PerDraw UBO bytes + hwm. Flushed to the uniform ring in drawIndexed
    // when the shader declares a set=2 UBO.
    std::array<std::uint8_t, kPerDrawStagingBytes> perDrawStaging{};
    std::uint32_t                                  perDrawDirtyEnd = 0;

    void reset() {
        currentVertex      = VK_NULL_HANDLE;
        currentIndex       = VK_NULL_HANDLE;
        currentCmd         = VK_NULL_HANDLE;
        passActive         = false;
        currentShader      = nullptr;
        currentVertexInput = nullptr;
        ubos.fill(VK_NULL_HANDLE);
        uboSizes.fill(0);
        texturesBySlot.fill(nullptr);
        materialTextures.fill(nullptr);
        pushConstantDirtyEnd = 0;
        perDrawDirtyEnd      = 0;
    }

    // Call after the draw consumes pending per-draw state; keeps the
    // shader+buffer bindings so subsequent draws from the same material
    // don't need to re-record them.
    void clearDrawScopedState() {
        texturesBySlot.fill(nullptr);
        materialTextures.fill(nullptr);
        pushConstantDirtyEnd = 0;
        perDrawDirtyEnd      = 0;
    }
};

} // namespace sonnet::renderer::vulkan
