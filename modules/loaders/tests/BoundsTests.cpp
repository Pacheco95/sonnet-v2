#include <sonnet/loaders/ModelLoader.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>

using namespace sonnet::loaders;
using namespace sonnet::api::render;

// Build a minimal CPUMesh containing only positions (no other attributes).
static CPUMesh makeMesh(std::initializer_list<glm::vec3> positions) {
    KnownAttributeSet posAttr;
    posAttr.insert(PositionAttribute{});
    VertexLayout layout{posAttr};

    std::vector<CPUMesh::Index> indices;
    for (CPUMesh::Index i = 0; i < static_cast<CPUMesh::Index>(positions.size()); ++i)
        indices.push_back(i);

    CPUMesh mesh{layout, std::move(indices), positions.size()};
    for (const auto &p : positions) {
        KnownAttributeSet attrs;
        attrs.insert(PositionAttribute{p});
        mesh.addVertex(attrs);
    }
    return mesh;
}

TEST_CASE("computeBoundingSphere: empty mesh returns zero radius", "[bounds]") {
    KnownAttributeSet posAttr;
    posAttr.insert(PositionAttribute{});
    CPUMesh empty{VertexLayout{posAttr}, {}};

    auto [center, radius] = computeBoundingSphere(empty);
    REQUIRE_THAT(radius, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("computeBoundingSphere: single vertex gives zero radius at that point", "[bounds]") {
    auto mesh = makeMesh({{5.0f, 3.0f, -2.0f}});
    auto [center, radius] = computeBoundingSphere(mesh);
    REQUIRE_THAT(center.x, Catch::Matchers::WithinAbs(5.0f, 1e-5f));
    REQUIRE_THAT(center.y, Catch::Matchers::WithinAbs(3.0f, 1e-5f));
    REQUIRE_THAT(center.z, Catch::Matchers::WithinAbs(-2.0f, 1e-5f));
    REQUIRE_THAT(radius,   Catch::Matchers::WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("computeBoundingSphere: two symmetric vertices have centroid at origin", "[bounds]") {
    auto mesh = makeMesh({{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    auto [center, radius] = computeBoundingSphere(mesh);
    REQUIRE_THAT(center.x, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(center.y, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(center.z, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(radius,   Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("computeBoundingSphere: radius encloses all vertices", "[bounds]") {
    // Triangle with vertices at (-1,0,0), (1,0,0), (0,2,0).
    // Centroid = (0, 2/3, 0).
    // Farthest vertex: distance from (0, 2/3, 0) to (0, 2, 0) = 4/3.
    // Distance from (0, 2/3, 0) to (±1, 0, 0) = sqrt(1 + 4/9) = sqrt(13)/3 ≈ 1.20.
    // So radius ≈ 4/3 ≈ 1.333.
    auto mesh = makeMesh({{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}});
    auto [center, radius] = computeBoundingSphere(mesh);

    // All vertices must be within the sphere.
    for (const auto &p : std::initializer_list<glm::vec3>{
            {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}) {
        const float dist = glm::length(p - center);
        REQUIRE(dist <= radius + 1e-5f);
    }

    // Centroid is correct.
    REQUIRE_THAT(center.x, Catch::Matchers::WithinAbs(0.0f,       1e-5f));
    REQUIRE_THAT(center.y, Catch::Matchers::WithinAbs(2.0f / 3.0f, 1e-5f));
    REQUIRE_THAT(center.z, Catch::Matchers::WithinAbs(0.0f,       1e-5f));
}

TEST_CASE("computeBoundingSphere: unit-cube corners give centroid at origin", "[bounds]") {
    // 8 corners of a unit cube (±0.5 on each axis).
    auto mesh = makeMesh({
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f},
    });
    auto [center, radius] = computeBoundingSphere(mesh);

    REQUIRE_THAT(center.x, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(center.y, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(center.z, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    // Radius = distance to a corner = sqrt(3 * 0.25) = sqrt(0.75).
    REQUIRE_THAT(radius, Catch::Matchers::WithinAbs(std::sqrt(0.75f), 1e-5f));
}
