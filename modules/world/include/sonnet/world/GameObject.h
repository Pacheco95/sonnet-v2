#pragma once

#include <sonnet/api/render/Material.h>
#include <sonnet/core/Types.h>
#include <sonnet/world/Transform.h>

#include <optional>
#include <string>

namespace sonnet::world {

struct RenderComponent {
    core::GPUMeshHandle              mesh;
    api::render::MaterialInstance    material;
};

class GameObject {
public:
    explicit GameObject(std::string name) : name(std::move(name)) {}

    std::string               name;
    Transform                 transform;
    std::optional<RenderComponent> render;
};

} // namespace sonnet::world
