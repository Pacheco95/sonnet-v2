#include <sonnet/world/Scene.h>

#include <algorithm>

namespace sonnet::world {

GameObject &Scene::createObject(std::string name, GameObject *parent) {
    m_objects.push_back(std::make_unique<GameObject>(std::move(name)));
    GameObject &obj = *m_objects.back();
    if (parent)
        obj.transform.setParent(&parent->transform, /*keepWorldTransform=*/false);
    return obj;
}

GameObject &Scene::duplicateObject(const GameObject &src) {
    // Resolve parent GameObject from the source's parent Transform pointer.
    GameObject *parent = nullptr;
    if (auto *pt = src.transform.getParent()) {
        for (auto &o : m_objects)
            if (&o->transform == pt) { parent = o.get(); break; }
    }

    // Clone a single node's components (not its children).
    auto cloneNode = [&](const GameObject &s, GameObject *p,
                         const std::string &name) -> GameObject & {
        auto &d = createObject(name, p);
        d.transform.setLocalPosition(s.transform.getLocalPosition());
        d.transform.setLocalRotation(s.transform.getLocalRotation());
        d.transform.setLocalScale(s.transform.getLocalScale());
        if (s.render)  d.render  = s.render;
        if (s.light)   d.light   = s.light;
        if (s.camera)  d.camera  = s.camera;
        // SkinComponent bone-transform pointers reference the *original* skeleton;
        // duplicating them is intentionally skipped (would need full skeleton remap).
        // AnimationPlayer is also not copied.
        return d;
    };

    // Recursively clone children, rooted at dstParent.
    // We look up each original child by Transform* in m_objects — safe because
    // duplicate objects have different Transform addresses.
    std::function<void(const GameObject &, GameObject *)> cloneChildren =
        [&](const GameObject &s, GameObject *dstParent) {
            for (auto *childTf : s.transform.children()) {
                for (auto &o : m_objects) {
                    if (&o->transform == childTf) {
                        auto &childDup = cloneNode(*o, dstParent, o->name);
                        cloneChildren(*o, &childDup);
                        break;
                    }
                }
            }
        };

    auto &root = cloneNode(src, parent, src.name + " (Copy)");
    cloneChildren(src, &root);
    return root;
}

void Scene::destroyObject(GameObject *obj) {
    if (!obj) return;

    // Snapshot children — the live list changes as children are removed.
    const std::vector<Transform *> kids = obj->transform.children();
    for (auto *childTf : kids) {
        for (auto &o : m_objects) {
            if (&o->transform == childTf) { destroyObject(o.get()); break; }
        }
    }

    // Detach from parent (removes this transform from the parent's child list).
    obj->transform.setParent(nullptr);

    // Erase and destruct via unique_ptr.
    m_objects.erase(
        std::remove_if(m_objects.begin(), m_objects.end(),
                       [obj](const auto &p) { return p.get() == obj; }),
        m_objects.end());
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
