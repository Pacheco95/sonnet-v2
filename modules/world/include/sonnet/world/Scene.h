#pragma once

#include <sonnet/api/render/RenderItem.h>
#include <sonnet/world/GameObject.h>

#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace sonnet::world {

class Scene {
public:
    // Create an object. If parent is non-null the object's transform is
    // attached to the parent's transform (keepWorldTransform = false).
    [[nodiscard]] GameObject &createObject(std::string name,
                                           GameObject  *parent = nullptr);

    // Shallow-clone src: copies Transform, RenderComponent, LightComponent, and
    // CameraComponent. AnimationPlayer and SkinComponent are not duplicated.
    // The clone is given the same parent and the name "<src.name> (Copy)".
    [[nodiscard]] GameObject &duplicateObject(const GameObject &src);

    // Remove obj and every descendant from the scene.
    // The caller is responsible for clearing any raw pointers to the removed
    // objects (e.g. selection state, script instances) before calling this.
    void destroyObject(GameObject *obj);

    // Append a RenderItem for every enabled object that has a RenderComponent.
    // If frustumPlanes is non-null, objects whose world-space bounding sphere
    // lies completely outside any plane are skipped.
    void buildRenderQueue(std::vector<api::render::RenderItem>  &queue,
                          const std::array<glm::vec4, 6>        *frustumPlanes = nullptr) const;

    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>> &objects() const {
        return m_objects;
    }

private:
    std::vector<std::unique_ptr<GameObject>> m_objects;
};

} // namespace sonnet::world
