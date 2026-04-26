#include <catch2/catch_test_macros.hpp>

#include <sonnet/api/render/VertexAttribute.h>

using namespace sonnet::api::render;

TEST_CASE("VertexAttribute: compile-time traits per type", "[vertex][attribute]") {
    STATIC_REQUIRE(PositionAttribute::location == 0);
    STATIC_REQUIRE(ColorAttribute::location == 1);
    STATIC_REQUIRE(TexCoordAttribute::location == 2);
    STATIC_REQUIRE(NormalAttribute::location == 3);
    STATIC_REQUIRE(TangentAttribute::location == 4);
    STATIC_REQUIRE(BiTangentAttribute::location == 5);
    STATIC_REQUIRE(BoneIndexAttribute::location == 6);
    STATIC_REQUIRE(BoneWeightAttribute::location == 7);

    STATIC_REQUIRE(PositionAttribute::sizeInBytes  == sizeof(glm::vec3));
    STATIC_REQUIRE(ColorAttribute::sizeInBytes     == sizeof(glm::vec4));
    STATIC_REQUIRE(TexCoordAttribute::sizeInBytes  == sizeof(glm::vec2));
    STATIC_REQUIRE(BoneIndexAttribute::sizeInBytes == sizeof(glm::ivec4));

    STATIC_REQUIRE(PositionAttribute::componentCount == 3);
    STATIC_REQUIRE(ColorAttribute::componentCount    == 4);
    STATIC_REQUIRE(TexCoordAttribute::componentCount == 2);
    STATIC_REQUIRE(BoneIndexAttribute::componentCount == 4);

    REQUIRE(PositionAttribute::name  == "Position");
    REQUIRE(ColorAttribute::name     == "Color");
    REQUIRE(NormalAttribute::name    == "Normal");
    REQUIRE(BoneWeightAttribute::name == "BoneWeight");
}

TEST_CASE("VertexAttribute: defaults zero-initialise value, normalize=false", "[vertex][attribute]") {
    PositionAttribute p{};
    REQUIRE(p.value == glm::vec3{0.0f});
    REQUIRE_FALSE(p.normalize);
}

TEST_CASE("VertexAttribute: operator<=> orders by location for same value type", "[vertex][attribute]") {
    PositionAttribute a{};
    NormalAttribute   b{};
    REQUIRE((a <=> b) < 0);
    REQUIRE((b <=> a) > 0);
    REQUIRE((a <=> PositionAttribute{}) == 0);
}

// NOTE: VertexAttribute<...>::Hasher is not exercised here — it calls
// core::hashCombine on the glm-typed `value`, which falls back to std::hash<glm::vecN>.
// That specialisation does not exist, so instantiating the hasher fails to compile.
// The hasher is currently dead code (CPUMesh and VertexLayout use their own).

TEST_CASE("Attribute concept matches the eight known attributes", "[vertex][attribute][concept]") {
    STATIC_REQUIRE(Attribute<PositionAttribute>);
    STATIC_REQUIRE(Attribute<ColorAttribute>);
    STATIC_REQUIRE(Attribute<TexCoordAttribute>);
    STATIC_REQUIRE(Attribute<NormalAttribute>);
    STATIC_REQUIRE(Attribute<TangentAttribute>);
    STATIC_REQUIRE(Attribute<BiTangentAttribute>);
    STATIC_REQUIRE(Attribute<BoneIndexAttribute>);
    STATIC_REQUIRE(Attribute<BoneWeightAttribute>);

    struct Bogus {};
    STATIC_REQUIRE_FALSE(Attribute<Bogus>);
    STATIC_REQUIRE_FALSE(Attribute<int>);
}

TEST_CASE("AttributeLocationComparator: orders KnownAttribute by location", "[vertex][attribute]") {
    AttributeLocationComparator cmp{};
    KnownAttribute pos = PositionAttribute{};
    KnownAttribute nrm = NormalAttribute{};
    REQUIRE(cmp(pos, nrm));
    REQUIRE_FALSE(cmp(nrm, pos));
    REQUIRE_FALSE(cmp(pos, pos));
}
