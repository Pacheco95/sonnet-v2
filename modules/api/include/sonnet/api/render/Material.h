#pragma once

#include <sonnet/api/render/RenderState.h>
#include <sonnet/core/Types.h>

#include <string>
#include <unordered_map>

namespace sonnet::api::render {

// Shared shader + render state used by multiple objects.
struct MaterialTemplate {
    core::ShaderHandle shaderHandle{};
    RenderState        renderState{};
};

// Per-object uniform overrides applied on top of a MaterialTemplate.
class MaterialInstance {
public:
    explicit MaterialInstance(core::MaterialTemplateHandle handle) : m_handle(handle) {}

    [[nodiscard]] core::MaterialTemplateHandle templateHandle() const { return m_handle; }

    template <typename T>
    void set(const std::string &name, const T &v) {
        m_values[name] = v;
    }

    [[nodiscard]] const core::UniformValue *tryGet(const std::string &name) const {
        auto it = m_values.find(name);
        return it != m_values.end() ? &it->second : nullptr;
    }

    void addTexture(const std::string &name, core::GPUTextureHandle handle) {
        m_textures[name] = handle;
    }

    [[nodiscard]] const std::unordered_map<std::string, core::UniformValue>    &values()      const { return m_values; }
    [[nodiscard]] const std::unordered_map<std::string, core::GPUTextureHandle> &getTextures() const { return m_textures; }

private:
    core::MaterialTemplateHandle                               m_handle;
    std::unordered_map<std::string, core::UniformValue>        m_values{};
    std::unordered_map<std::string, core::GPUTextureHandle>    m_textures{};
};

} // namespace sonnet::api::render
