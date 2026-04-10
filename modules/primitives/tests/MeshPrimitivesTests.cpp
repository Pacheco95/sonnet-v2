#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <sonnet/primitives/MeshPrimitives.h>

#include <cstring>

using namespace sonnet::primitives;
using namespace sonnet::api::render;

// ── Helpers to read typed fields from raw interleaved vertex data ─────────────
//
// PNT layout (UVSphere, Quad):
//   offset  0: Position  (vec3, 12 bytes)  location=0
//   offset 12: TexCoord  (vec2,  8 bytes)  location=2
//   offset 20: Normal    (vec3, 12 bytes)  location=3
//   stride = 32 bytes
//
// PNTBT layout (Box):
//   offset  0: Position   (vec3, 12 bytes)  location=0
//   offset 12: TexCoord   (vec2,  8 bytes)  location=2
//   offset 20: Normal     (vec3, 12 bytes)  location=3
//   offset 32: Tangent    (vec3, 12 bytes)  location=4
//   offset 44: BiTangent  (vec3, 12 bytes)  location=5
//   stride = 56 bytes

static constexpr std::size_t STRIDE_PNT   = 32;
static constexpr std::size_t STRIDE_PNTBT = 56;

static constexpr std::size_t OFFSET_POS      =  0;
static constexpr std::size_t OFFSET_TEXCOORD = 12;
static constexpr std::size_t OFFSET_NORMAL   = 20;
static constexpr std::size_t OFFSET_TANGENT  = 32;
static constexpr std::size_t OFFSET_BITAN    = 44;

static glm::vec3 readVec3(const CPUMesh &mesh, std::size_t stride,
                           std::size_t vertexIndex, std::size_t byteOffset) {
    glm::vec3 v{};
    std::memcpy(&v, mesh.rawData().data() + vertexIndex * stride + byteOffset, sizeof(v));
    return v;
}

static glm::vec2 readVec2(const CPUMesh &mesh, std::size_t stride,
                           std::size_t vertexIndex, std::size_t byteOffset) {
    glm::vec2 v{};
    std::memcpy(&v, mesh.rawData().data() + vertexIndex * stride + byteOffset, sizeof(v));
    return v;
}

// ── Box ───────────────────────────────────────────────────────────────────────

TEST_CASE("makeBox: vertex and index counts", "[primitives]") {
    const auto mesh = makeBox({1.0f, 1.0f, 1.0f});
    CHECK(mesh.vertexCount() == 24); // 4 vertices per face × 6 faces
    CHECK(mesh.indices().size() == 36); // 6 indices per face × 6 faces
}

TEST_CASE("makeBox: stride matches PNTBT layout", "[primitives]") {
    const auto mesh = makeBox({1.0f, 1.0f, 1.0f});
    CHECK(mesh.layout().getStride() == STRIDE_PNTBT);
}

TEST_CASE("makeBox: all normals are unit length", "[primitives]") {
    const auto mesh = makeBox({2.0f, 3.0f, 1.5f});
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec3 n = readVec3(mesh, STRIDE_PNTBT, i, OFFSET_NORMAL);
        CHECK(glm::length(n) == Catch::Approx(1.0f).epsilon(1e-5f));
    }
}

TEST_CASE("makeBox: all tangents are unit length", "[primitives]") {
    const auto mesh = makeBox({2.0f, 3.0f, 1.5f});
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec3 t = readVec3(mesh, STRIDE_PNTBT, i, OFFSET_TANGENT);
        CHECK(glm::length(t) == Catch::Approx(1.0f).epsilon(1e-5f));
    }
}

TEST_CASE("makeBox: all bitangents are unit length", "[primitives]") {
    const auto mesh = makeBox({2.0f, 3.0f, 1.5f});
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec3 b = readVec3(mesh, STRIDE_PNTBT, i, OFFSET_BITAN);
        CHECK(glm::length(b) == Catch::Approx(1.0f).epsilon(1e-5f));
    }
}

TEST_CASE("makeBox: UVs are in [0,1] range", "[primitives]") {
    const auto mesh = makeBox({1.0f, 1.0f, 1.0f});
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec2 uv = readVec2(mesh, STRIDE_PNTBT, i, OFFSET_TEXCOORD);
        CHECK(uv.x >= 0.0f);
        CHECK(uv.y >= 0.0f);
    }
}

// ── UVSphere ──────────────────────────────────────────────────────────────────

TEST_CASE("makeUVSphere smooth: vertex and index counts", "[primitives]") {
    constexpr int segX = 16, segY = 8;
    const auto mesh = makeUVSphere(segX, segY, /*smooth=*/true);
    CHECK(mesh.vertexCount() == static_cast<std::size_t>((segX + 1) * (segY + 1)));
    CHECK(mesh.indices().size() == static_cast<std::size_t>(segX * segY * 6));
}

TEST_CASE("makeUVSphere smooth: all normals are unit length", "[primitives]") {
    const auto mesh = makeUVSphere(12, 6, true);
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec3 n = readVec3(mesh, STRIDE_PNT, i, OFFSET_NORMAL);
        CHECK(glm::length(n) == Catch::Approx(1.0f).epsilon(1e-5f));
    }
}

TEST_CASE("makeUVSphere smooth: UVs are in [0,1]", "[primitives]") {
    const auto mesh = makeUVSphere(12, 6, true);
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec2 uv = readVec2(mesh, STRIDE_PNT, i, OFFSET_TEXCOORD);
        CHECK(uv.x >= -1e-5f);
        CHECK(uv.x <= 1.0f + 1e-5f);
        CHECK(uv.y >= -1e-5f);
        CHECK(uv.y <= 1.0f + 1e-5f);
    }
}

// ── Quad ──────────────────────────────────────────────────────────────────────

TEST_CASE("makeQuad: vertex and index counts", "[primitives]") {
    const auto mesh = makeQuad({1.0f, 1.0f});
    CHECK(mesh.vertexCount() == 4);
    CHECK(mesh.indices().size() == 6);
}

TEST_CASE("makeQuad: normal is (0,0,1)", "[primitives]") {
    const auto mesh = makeQuad({2.0f, 3.0f});
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec3 n = readVec3(mesh, STRIDE_PNT, i, OFFSET_NORMAL);
        CHECK(n.x == Catch::Approx(0.0f));
        CHECK(n.y == Catch::Approx(0.0f));
        CHECK(n.z == Catch::Approx(1.0f));
    }
}

TEST_CASE("makeQuad: UVs are in [0,1]", "[primitives]") {
    const auto mesh = makeQuad({1.0f, 1.0f});
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const glm::vec2 uv = readVec2(mesh, STRIDE_PNT, i, OFFSET_TEXCOORD);
        CHECK(uv.x >= -1e-5f);
        CHECK(uv.x <= 1.0f + 1e-5f);
        CHECK(uv.y >= -1e-5f);
        CHECK(uv.y <= 1.0f + 1e-5f);
    }
}
