#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sonnet/scene/SceneLoader.h>
#include <sonnet/world/Scene.h>

using namespace sonnet;

// All tests pass renderer=nullptr so no GPU is required.

TEST_CASE("SceneLoader: creates objects with correct names", "[scene_loader]") {
    world::Scene     scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [
            { "name": "Alpha" },
            { "name": "Beta"  }
        ]
    })", "", scene);

    REQUIRE(scene.objects().size() == 2);
    REQUIRE(scene.objects()[0]->name == "Alpha");
    REQUIRE(scene.objects()[1]->name == "Beta");
}

TEST_CASE("SceneLoader: returns object map with correct pointers", "[scene_loader]") {
    world::Scene     scene;
    scene::SceneLoader loader;

    auto loaded = loader.loadFromString(R"({
        "objects": [ { "name": "Node" } ]
    })", "", scene);

    REQUIRE(loaded.objects.count("Node") == 1);
    REQUIRE(loaded.objects.at("Node") == scene.objects()[0].get());
}

TEST_CASE("SceneLoader: applies position to transform", "[scene_loader]") {
    world::Scene     scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [
            { "name": "Obj", "position": [1.0, 2.0, 3.0] }
        ]
    })", "", scene);

    const auto &pos = scene.objects()[0]->transform.getLocalPosition();
    REQUIRE_THAT(pos.x, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(pos.y, Catch::Matchers::WithinAbs(2.0f, 1e-5f));
    REQUIRE_THAT(pos.z, Catch::Matchers::WithinAbs(3.0f, 1e-5f));
}

TEST_CASE("SceneLoader: establishes parent-child relationship", "[scene_loader]") {
    world::Scene     scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [
            { "name": "Parent", "position": [10.0, 0.0, 0.0] },
            { "name": "Child",  "parent": "Parent", "position": [1.0, 0.0, 0.0] }
        ]
    })", "", scene);

    const auto &child = *scene.objects()[1];
    REQUIRE(child.transform.getParent() != nullptr);
    // World position = parent(10) + local(1) = 11
    REQUIRE_THAT(child.transform.getWorldPosition().x,
                 Catch::Matchers::WithinAbs(11.0f, 1e-4f));
}

TEST_CASE("SceneLoader: sets camera component", "[scene_loader]") {
    world::Scene     scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [
            { "name": "Cam", "camera": { "fov": 75.0, "near": 0.5, "far": 500.0 } }
        ]
    })", "", scene);

    const auto &obj = *scene.objects()[0];
    REQUIRE(obj.camera.has_value());
    REQUIRE_THAT(obj.camera->fov,  Catch::Matchers::WithinAbs(75.0f,  1e-5f));
    REQUIRE_THAT(obj.camera->near, Catch::Matchers::WithinAbs( 0.5f,  1e-5f));
    REQUIRE_THAT(obj.camera->far,  Catch::Matchers::WithinAbs(500.0f, 1e-5f));
}

TEST_CASE("SceneLoader: object with no render section has no render component", "[scene_loader]") {
    world::Scene     scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [ { "name": "Empty" } ]
    })", "", scene);

    REQUIRE_FALSE(scene.objects()[0]->render.has_value());
}

TEST_CASE("SceneLoader: enabled=false object has enabled flag false", "[scene_loader]") {
    world::Scene scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [
            { "name": "On"  },
            { "name": "Off", "enabled": false }
        ]
    })", "", scene);

    REQUIRE( scene.objects()[0]->enabled);
    REQUIRE(!scene.objects()[1]->enabled);
}

TEST_CASE("SceneLoader: object with light component gets LightComponent", "[scene_loader]") {
    world::Scene scene;
    scene::SceneLoader loader;

    loader.loadFromString(R"({
        "objects": [
            { "name": "Sun", "light": { "type": "directional" } }
        ]
    })", "", scene);

    REQUIRE(scene.objects()[0]->light.has_value());
}

TEST_CASE("SceneLoader: directional light fields parsed correctly", "[scene_loader]") {
    world::Scene scene;
    scene::SceneLoader loader;

    auto loaded = loader.loadFromString(R"({
        "objects": [{
            "name": "Sun",
            "light": {
                "type":      "directional",
                "color":     [0.8, 0.9, 1.0],
                "intensity": 3.5,
                "direction": [0.0, -1.0, 0.0]
            }
        }]
    })", "", scene);

    const auto &lc = scene.objects()[0]->light;
    REQUIRE(lc.has_value());
    REQUIRE(lc->type == world::LightComponent::Type::Directional);
    REQUIRE_THAT(lc->intensity, Catch::Matchers::WithinAbs(3.5f, 1e-5f));
    REQUIRE_THAT(lc->color.r,   Catch::Matchers::WithinAbs(0.8f, 1e-5f));

    REQUIRE(loaded.directionalLights.size() == 1);
    REQUIRE_THAT(loaded.directionalLights[0].intensity,
                 Catch::Matchers::WithinAbs(3.5f, 1e-5f));
}

TEST_CASE("SceneLoader: point light fields parsed correctly", "[scene_loader]") {
    world::Scene scene;
    scene::SceneLoader loader;

    auto loaded = loader.loadFromString(R"({
        "objects": [{
            "name": "Lamp",
            "position": [2.0, 4.0, 0.0],
            "light": {
                "type":      "point",
                "color":     [1.0, 0.5, 0.2],
                "intensity": 8.0
            }
        }]
    })", "", scene);

    const auto &lc = scene.objects()[0]->light;
    REQUIRE(lc.has_value());
    REQUIRE(lc->type == world::LightComponent::Type::Point);
    REQUIRE_THAT(lc->intensity, Catch::Matchers::WithinAbs(8.0f, 1e-5f));
    REQUIRE_THAT(lc->color.g,   Catch::Matchers::WithinAbs(0.5f, 1e-5f));

    REQUIRE(loaded.pointLights.size() == 1);
    REQUIRE_THAT(loaded.pointLights[0].intensity,
                 Catch::Matchers::WithinAbs(8.0f, 1e-5f));
}
