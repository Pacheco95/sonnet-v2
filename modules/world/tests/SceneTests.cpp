#include <sonnet/world/Scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/gtc/quaternion.hpp>

using namespace sonnet;
using namespace sonnet::world;

// Construct a 6-plane frustum that keeps objects with z in (-1000, 5).
// Plane convention: dot(normal, center) + d < -radius → outside.
static std::array<glm::vec4, 6> makeFarPlaneFrustum(float maxZ = 5.0f) {
    return {
        glm::vec4{ 1.0f, 0.0f,  0.0f, 1000.0f},  // left:   x > -1000
        glm::vec4{-1.0f, 0.0f,  0.0f, 1000.0f},  // right:  x <  1000
        glm::vec4{ 0.0f, 1.0f,  0.0f, 1000.0f},  // bottom: y > -1000
        glm::vec4{ 0.0f,-1.0f,  0.0f, 1000.0f},  // top:    y <  1000
        glm::vec4{ 0.0f, 0.0f,  1.0f, 1000.0f},  // near:   z > -1000
        glm::vec4{ 0.0f, 0.0f, -1.0f, maxZ},      // far:    z < maxZ
    };
}

TEST_CASE("Scene::createObject returns a usable reference", "[scene]") {
    Scene scene;
    [[maybe_unused]] auto &obj = scene.createObject("Cube");
    obj.transform.setLocalPosition({1.0f, 2.0f, 3.0f});

    REQUIRE(scene.objects().size() == 1);
    REQUIRE(scene.objects()[0]->name == "Cube");
}

TEST_CASE("Scene::buildRenderQueue skips objects without RenderComponent", "[scene]") {
    Scene scene;
    [[maybe_unused]] auto &empty = scene.createObject("Empty");

    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue);
    REQUIRE(queue.empty());
}

TEST_CASE("Scene::buildRenderQueue includes objects with RenderComponent", "[scene]") {
    Scene scene;
    auto &obj = scene.createObject("Box");
    obj.render = RenderComponent{
        .mesh     = core::GPUMeshHandle{42},
        .material = api::render::MaterialInstance{core::MaterialTemplateHandle{7}},
    };
    obj.transform.setLocalPosition({1.0f, 0.0f, 0.0f});

    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue);

    REQUIRE(queue.size() == 1);
    REQUIRE(queue[0].name == "Box");
    REQUIRE(queue[0].mesh == core::GPUMeshHandle{42});
    // worldMatrix translation column should reflect the local position.
    REQUIRE_THAT(queue[0].modelMatrix[3].x, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Scene: child object world matrix includes parent transform", "[scene]") {
    Scene scene;
    auto &parent = scene.createObject("Parent");
    parent.transform.setLocalPosition({10.0f, 0.0f, 0.0f});

    auto &child = scene.createObject("Child", &parent);
    child.transform.setLocalPosition({1.0f, 0.0f, 0.0f});
    child.render = RenderComponent{
        .mesh     = core::GPUMeshHandle{1},
        .material = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
    };

    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue);

    REQUIRE(queue.size() == 1);
    // World position = parent(10) + local(1) = 11
    REQUIRE_THAT(queue[0].modelMatrix[3].x, Catch::Matchers::WithinAbs(11.0f, 1e-5f));
}

TEST_CASE("Scene: parent rotation is inherited by child world matrix", "[scene]") {
    Scene scene;
    auto &parent = scene.createObject("Parent");
    // 90° around Y: maps +X to -Z
    parent.transform.setLocalRotation(
        glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}));

    auto &child = scene.createObject("Child", &parent);
    child.transform.setLocalPosition({1.0f, 0.0f, 0.0f});
    child.render = RenderComponent{
        .mesh     = core::GPUMeshHandle{1},
        .material = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
    };

    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue);

    REQUIRE(queue.size() == 1);
    // After 90° Y rotation, child at local +X lands at world -Z
    REQUIRE_THAT(queue[0].modelMatrix[3].x, Catch::Matchers::WithinAbs( 0.0f, 1e-4f));
    REQUIRE_THAT(queue[0].modelMatrix[3].z, Catch::Matchers::WithinAbs(-1.0f, 1e-4f));
}

TEST_CASE("Scene::buildRenderQueue uses worldMatrix from Transform", "[scene]") {
    Scene scene;
    auto &obj = scene.createObject("Scaled");
    obj.render = RenderComponent{
        .mesh     = core::GPUMeshHandle{1},
        .material = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
    };
    obj.transform.setLocalScale({2.0f, 2.0f, 2.0f});

    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue);

    REQUIRE(queue.size() == 1);
    // Scale 2 means the first column of the model matrix has magnitude 2.
    const float scaleX = glm::length(glm::vec3{queue[0].modelMatrix[0]});
    REQUIRE_THAT(scaleX, Catch::Matchers::WithinAbs(2.0f, 1e-5f));
}

// ── Frustum culling ───────────────────────────────────────────────────────────

TEST_CASE("Scene::buildRenderQueue with frustum culls objects beyond far plane", "[scene]") {
    Scene scene;

    auto &near = scene.createObject("Near");
    near.transform.setLocalPosition({0.0f, 0.0f, 0.0f});
    near.render = RenderComponent{
        .mesh         = core::GPUMeshHandle{1},
        .material     = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
        .boundsRadius = 0.1f,
    };

    auto &far = scene.createObject("Far");
    far.transform.setLocalPosition({0.0f, 0.0f, 100.0f});
    far.render = RenderComponent{
        .mesh         = core::GPUMeshHandle{2},
        .material     = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
        .boundsRadius = 0.1f,
    };

    const auto frustum = makeFarPlaneFrustum(5.0f);
    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue, &frustum);

    REQUIRE(queue.size() == 1);
    REQUIRE(queue[0].name == "Near");
}

TEST_CASE("Scene::buildRenderQueue with frustum keeps objects inside frustum", "[scene]") {
    Scene scene;

    for (int i = 0; i < 3; ++i) {
        auto &obj = scene.createObject("Inside" + std::to_string(i));
        obj.transform.setLocalPosition({0.0f, 0.0f, static_cast<float>(i)});
        obj.render = RenderComponent{
            .mesh         = core::GPUMeshHandle{static_cast<std::size_t>(i + 1)},
            .material     = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
            .boundsRadius = 0.1f,
        };
    }

    const auto frustum = makeFarPlaneFrustum(10.0f);
    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue, &frustum);

    REQUIRE(queue.size() == 3);
}

TEST_CASE("Scene::buildRenderQueue nullptr frustum includes all objects", "[scene]") {
    Scene scene;
    for (int i = 0; i < 5; ++i) {
        auto &obj = scene.createObject("Obj" + std::to_string(i));
        obj.transform.setLocalPosition({0.0f, 0.0f, static_cast<float>(i * 1000)});
        obj.render = RenderComponent{
            .mesh         = core::GPUMeshHandle{static_cast<std::size_t>(i + 1)},
            .material     = api::render::MaterialInstance{core::MaterialTemplateHandle{1}},
            .boundsRadius = 0.1f,
        };
    }

    std::vector<api::render::RenderItem> queue;
    scene.buildRenderQueue(queue, nullptr);
    REQUIRE(queue.size() == 5);
}

// ── duplicateObject ───────────────────────────────────────────────────────────

TEST_CASE("Scene::duplicateObject copies transform and name", "[scene]") {
    Scene scene;
    auto &src = scene.createObject("Box");
    src.transform.setLocalPosition({3.0f, 1.0f, -2.0f});

    auto &dup = scene.duplicateObject(src);

    REQUIRE(dup.name == "Box (Copy)");
    REQUIRE_THAT(dup.transform.getLocalPosition().x, Catch::Matchers::WithinAbs(3.0f, 1e-5f));
    REQUIRE_THAT(dup.transform.getLocalPosition().y, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(dup.transform.getLocalPosition().z, Catch::Matchers::WithinAbs(-2.0f, 1e-5f));
    REQUIRE(scene.objects().size() == 2);
}

TEST_CASE("Scene::duplicateObject copies RenderComponent", "[scene]") {
    Scene scene;
    auto &src = scene.createObject("Mesh");
    src.render = RenderComponent{
        .mesh     = core::GPUMeshHandle{77},
        .material = api::render::MaterialInstance{core::MaterialTemplateHandle{3}},
    };

    auto &dup = scene.duplicateObject(src);

    REQUIRE(dup.render.has_value());
    REQUIRE(dup.render->mesh == core::GPUMeshHandle{77});
}

TEST_CASE("Scene::duplicateObject shares the same parent", "[scene]") {
    Scene scene;
    auto &parent = scene.createObject("Parent");
    auto &child  = scene.createObject("Child", &parent);

    auto &dup = scene.duplicateObject(child);

    // Both child and its duplicate should have parent as their Transform parent.
    REQUIRE(dup.transform.getParent() == &parent.transform);
}

// ── destroyObject ─────────────────────────────────────────────────────────────

TEST_CASE("Scene::destroyObject removes object from scene", "[scene]") {
    Scene scene;
    auto &a = scene.createObject("A");
    [[maybe_unused]] auto &b = scene.createObject("B");

    scene.destroyObject(&a);

    REQUIRE(scene.objects().size() == 1);
    REQUIRE(scene.objects()[0]->name == "B");
}

TEST_CASE("Scene::destroyObject also removes all descendants", "[scene]") {
    Scene scene;
    auto &root  = scene.createObject("Root");
    auto &child = scene.createObject("Child",       &root);
    [[maybe_unused]] auto &grandchild = scene.createObject("GrandChild", &child);

    scene.destroyObject(&root);

    REQUIRE(scene.objects().empty());
}

TEST_CASE("Scene::destroyObject nullptr is a no-op", "[scene]") {
    Scene scene;
    [[maybe_unused]] auto &obj = scene.createObject("Safe");
    REQUIRE_NOTHROW(scene.destroyObject(nullptr));
    REQUIRE(scene.objects().size() == 1);
}
