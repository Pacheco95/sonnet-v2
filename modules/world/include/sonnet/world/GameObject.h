#pragma once

#include <sonnet/api/render/Material.h>
#include <sonnet/core/Types.h>
#include <sonnet/world/AnimationPlayer.h>
#include <sonnet/world/CameraComponent.h>
#include <sonnet/world/SkinComponent.h>
#include <sonnet/world/Transform.h>

#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace sonnet::world {

struct RenderComponent {
    core::GPUMeshHandle              mesh;
    api::render::MaterialInstance    material;
};

struct LightComponent {
    enum class Type { Directional, Point } type = Type::Point;
    glm::vec3 color{1.0f};
    float     intensity{1.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // directional only
    bool      enabled{true};
};

class GameObject {
public:
    explicit GameObject(std::string name) : name(std::move(name)) {}

    std::string                       name;
    bool                              enabled{true};
    Transform                         transform;
    std::optional<RenderComponent>    render;
    std::optional<LightComponent>     light;
    std::optional<CameraComponent>    camera;
    std::optional<AnimationPlayer>    animationPlayer;
    std::optional<SkinComponent>      skin;
};

} // namespace sonnet::world
