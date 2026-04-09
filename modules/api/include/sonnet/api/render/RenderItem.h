#pragma once

#include <sonnet/api/render/Material.h>
#include <sonnet/core/Types.h>

#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace sonnet::api::render {

struct RenderItem {
    std::optional<std::string>   name;
    core::GPUMeshHandle          mesh;
    MaterialInstance             material;
    glm::mat4                    modelMatrix{1.0f};
};

} // namespace sonnet::api::render
