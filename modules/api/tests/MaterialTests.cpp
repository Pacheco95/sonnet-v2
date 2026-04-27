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

TEST_CASE("MaterialInstance: templateHandle returns the constructed handle", "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{42}};
    REQUIRE(inst.templateHandle() == MaterialTemplateHandle{42});
}

TEST_CASE("MaterialInstance: set round-trips multiple UniformValue variant types", "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{1}};

    inst.set("uInt",   7);
    inst.set("uFloat", 1.5f);
    inst.set("uVec2",  glm::vec2{1.0f, 2.0f});
    inst.set("uVec3",  glm::vec3{1.0f, 2.0f, 3.0f});
    inst.set("uVec4",  glm::vec4{1.0f, 2.0f, 3.0f, 4.0f});
    inst.set("uMat4",  glm::mat4{1.0f});
    inst.set("uTex",   sonnet::core::Sampler{3});

    REQUIRE(std::get<int>      (*inst.tryGet("uInt"))   == 7);
    REQUIRE_THAT(std::get<float>(*inst.tryGet("uFloat")), Catch::Matchers::WithinAbs(1.5f, 1e-6f));
    REQUIRE(std::get<glm::vec2>(*inst.tryGet("uVec2")) == glm::vec2{1.0f, 2.0f});
    REQUIRE(std::get<glm::vec3>(*inst.tryGet("uVec3")) == glm::vec3{1.0f, 2.0f, 3.0f});
    REQUIRE(std::get<glm::vec4>(*inst.tryGet("uVec4")) == glm::vec4{1.0f, 2.0f, 3.0f, 4.0f});
    REQUIRE(std::get<glm::mat4>(*inst.tryGet("uMat4")) == glm::mat4{1.0f});
    REQUIRE(std::get<sonnet::core::Sampler>(*inst.tryGet("uTex")) == sonnet::core::Sampler{3});

    REQUIRE(inst.values().size() == 7);
}

TEST_CASE("MaterialInstance: addTexture appends to the texture map", "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{1}};

    inst.addTexture("uAlbedo", sonnet::core::GPUTextureHandle{10});
    inst.addTexture("uNormal", sonnet::core::GPUTextureHandle{11});

    const auto &tex = inst.getTextures();
    REQUIRE(tex.size() == 2);
    REQUIRE(tex.at("uAlbedo") == sonnet::core::GPUTextureHandle{10});
    REQUIRE(tex.at("uNormal") == sonnet::core::GPUTextureHandle{11});
}

TEST_CASE("MaterialInstance: addTexture overwrites previous binding under same name",
         "[material]") {
    MaterialInstance inst{MaterialTemplateHandle{1}};

    inst.addTexture("uAlbedo", sonnet::core::GPUTextureHandle{10});
    inst.addTexture("uAlbedo", sonnet::core::GPUTextureHandle{20});

    const auto &tex = inst.getTextures();
    REQUIRE(tex.size() == 1);
    REQUIRE(tex.at("uAlbedo") == sonnet::core::GPUTextureHandle{20});
}

TEST_CASE("MaterialTemplate: defaults coexist with instance overrides without writeback",
         "[material]") {
    MaterialTemplate tmpl;
    tmpl.defaultValues["uExposure"] = 1.0f;

    MaterialInstance inst{MaterialTemplateHandle{1}};
    inst.set("uExposure", 2.5f);

    REQUIRE_THAT(std::get<float>(tmpl.defaultValues.at("uExposure")),
                 Catch::Matchers::WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(std::get<float>(*inst.tryGet("uExposure")),
                 Catch::Matchers::WithinAbs(2.5f, 1e-6f));
}
