#pragma once

#include <sonnet/api/render/RenderState.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>

namespace sonnet::renderer::vulkan {

class Device;
class VkShader;
class VkVertexInputState;

// Keyed cache of VkPipeline objects. The key is (shader, render state,
// vertex-input state, render pass). Pipelines are immutable once created;
// any state change (depth mode, blend, cull, vertex layout, etc.) will
// cause drawIndexed to fall through to getOrCreate and build a new entry.
//
// Viewport + scissor are declared as dynamic, so pipelines survive window
// resize without re-creation.
//
// Class is named `PipelineCache` (no Vk prefix) to avoid colliding with
// the Vulkan symbol ::VkPipelineCache, which we also use internally for
// Vulkan's driver-side pipeline cache.
class PipelineCache {
public:
    explicit PipelineCache(Device &device);
    ~PipelineCache();

    PipelineCache(const PipelineCache &)            = delete;
    PipelineCache &operator=(const PipelineCache &) = delete;

    VkPipeline getOrCreate(const VkShader                    &shader,
                           const api::render::RenderState    &state,
                           const VkVertexInputState          &vis,
                           VkRenderPass                       renderPass,
                           std::uint32_t                      colorAttachmentCount,
                           bool                               hasDepthAttachment);

    // Destroy every cached pipeline whose key references this shader. Called
    // from VkRendererBackend after Renderer::reloadShader recompiles a shader
    // in place — without this, cached pipelines keep binding the old shader
    // module via the still-live VkShader pointer. Safe to call only when no
    // command buffer that recorded one of the affected pipelines is still
    // in flight; reload runs between frames so this holds.
    void invalidateForShader(const VkShader *shader);

private:
    struct Key {
        const VkShader           *shader;
        const VkVertexInputState *vis;
        api::render::RenderState  state;
        VkRenderPass              renderPass;
        std::uint32_t             colorAttachmentCount;
        bool                      hasDepth;

        bool operator==(const Key &o) const noexcept {
            return shader == o.shader
                && vis == o.vis
                && state == o.state
                && renderPass == o.renderPass
                && colorAttachmentCount == o.colorAttachmentCount
                && hasDepth == o.hasDepth;
        }
    };

    struct Hasher {
        std::size_t operator()(const Key &k) const noexcept;
    };

    VkPipeline build(const Key &k);

    Device                                           &m_device;
    ::VkPipelineCache                                 m_vkCache = VK_NULL_HANDLE;
    std::unordered_map<Key, VkPipeline, Hasher>       m_map;
};

} // namespace sonnet::renderer::vulkan
