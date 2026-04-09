#include <sonnet/world/Scene.h>

namespace sonnet::world {

GameObject &Scene::createObject(std::string name) {
    m_objects.push_back(std::make_unique<GameObject>(std::move(name)));
    return *m_objects.back();
}

void Scene::buildRenderQueue(std::vector<api::render::RenderItem> &queue) const {
    for (const auto &obj : m_objects) {
        if (!obj->render) continue;
        queue.push_back({
            .name        = obj->name,
            .mesh        = obj->render->mesh,
            .material    = obj->render->material,
            .modelMatrix = obj->transform.getModelMatrix(),
        });
    }
}

} // namespace sonnet::world
