#include <sonnet/world/Scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace sonnet;
using namespace sonnet::world;

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
