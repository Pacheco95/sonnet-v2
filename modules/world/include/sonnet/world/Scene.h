#pragma once

#include <sonnet/api/render/RenderItem.h>
#include <sonnet/world/GameObject.h>

#include <memory>
#include <string>
#include <vector>

namespace sonnet::world {

class Scene {
public:
    // Create a root-level object and return a reference to it.
    [[nodiscard]] GameObject &createObject(std::string name);

    // Append a RenderItem for every object that has a RenderComponent.
    void buildRenderQueue(std::vector<api::render::RenderItem> &queue) const;

    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>> &objects() const {
        return m_objects;
    }

private:
    std::vector<std::unique_ptr<GameObject>> m_objects;
};

} // namespace sonnet::world
