#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sonnet/api/render/Material.h>
#include <sonnet/core/Types.h>

using namespace sonnet::api::render;
using sonnet::core::MaterialTemplateHandle;

TEST_CASE("MaterialTemplate: defaultValues are stored and accessible", "[material]") {
    MaterialTemplate tmpl;
    tmpl.defaultValues["uRoughness"] = 0.5f;
    tmpl.defaultValues["uMetallic"]  = 0.0f;

    REQUIRE(std::holds_alternative<float>(tmpl.defaultValues.at("uRoughness")));
    REQUIRE_THAT(std::get<float>(tmpl.defaultValues.at("uRoughness")),
                 Catch::Matchers::WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("MaterialInstance: set stores value retrievable via tryGet", "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{1}};
    inst.set("uBias", 0.005f);

    const auto *val = inst.tryGet("uBias");
    REQUIRE(val != nullptr);
    REQUIRE_THAT(std::get<float>(*val), Catch::Matchers::WithinAbs(0.005f, 1e-6f));
}

TEST_CASE("MaterialInstance: tryGet returns nullptr for absent key", "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{1}};
    REQUIRE(inst.tryGet("uMissing") == nullptr);
}

TEST_CASE("MaterialInstance: set overwrites previous value", "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{1}};
    inst.set("uExposure", 1.0f);
    inst.set("uExposure", 2.5f);

    const auto *val = inst.tryGet("uExposure");
    REQUIRE(val != nullptr);
    REQUIRE_THAT(std::get<float>(*val), Catch::Matchers::WithinAbs(2.5f, 1e-6f));
}

TEST_CASE("MaterialInstance: instance values do not affect other instances", "[material]") {
    MaterialInstance a{MaterialTemplateHandle{1}};
    MaterialInstance b{MaterialTemplateHandle{1}};

    a.set("uColor", glm::vec3{1.0f, 0.0f, 0.0f});

    REQUIRE(b.tryGet("uColor") == nullptr);
}
