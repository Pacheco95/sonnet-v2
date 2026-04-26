#include <catch2/catch_test_macros.hpp>

#include <sonnet/api/render/CPUMesh.h>

using namespace sonnet::api::render;

namespace {

VertexLayout posNormalLayout() {
    return VertexLayout{{PositionAttribute{}, NormalAttribute{}}};
}

} // namespace

TEST_CASE("CPUMesh: empty after construction", "[mesh][cpu]") {
    CPUMesh mesh{posNormalLayout(), {0, 1, 2}};
    REQUIRE(mesh.vertexCount() == 0);
    REQUIRE(mesh.bytes() == 0);
    REQUIRE(mesh.rawData().empty());
    REQUIRE(mesh.indices() == std::vector<CPUMesh::Index>{0, 1, 2});
    REQUIRE(mesh.layout().getStride() == sizeof(glm::vec3) * 2);
}

TEST_CASE("CPUMesh: addVertex appends stride bytes and increments count", "[mesh][cpu]") {
    CPUMesh mesh{posNormalLayout(), {0}};

    PositionAttribute p{};
    p.value = glm::vec3{1.0f, 2.0f, 3.0f};
    NormalAttribute n{};
    n.value = glm::vec3{0.0f, 1.0f, 0.0f};

    mesh.addVertex({p, n});

    REQUIRE(mesh.vertexCount() == 1);
    REQUIRE(mesh.bytes() == mesh.layout().getStride());
    REQUIRE(mesh.rawData().size() == sizeof(glm::vec3) * 2);
}

TEST_CASE("CPUMesh: addVertex returns *this for chaining", "[mesh][cpu]") {
    CPUMesh mesh{posNormalLayout(), {0, 1}};

    PositionAttribute p{};
    NormalAttribute   n{};

    auto &ret = mesh.addVertex({p, n}).addVertex({p, n});
    REQUIRE(&ret == &mesh);
    REQUIRE(mesh.vertexCount() == 2);
    REQUIRE(mesh.bytes() == 2 * mesh.layout().getStride());
}

TEST_CASE("CPUMesh: bytes equals vertexCount * stride after multiple adds", "[mesh][cpu]") {
    CPUMesh mesh{posNormalLayout(), {0, 1, 2}};
    PositionAttribute p{};
    NormalAttribute   n{};
    for (int i = 0; i < 5; ++i) mesh.addVertex({p, n});
    REQUIRE(mesh.bytes() == mesh.vertexCount() * mesh.layout().getStride());
}

TEST_CASE("CPUMesh: vertexCountHint reserves but does not size the buffer", "[mesh][cpu]") {
    CPUMesh mesh{posNormalLayout(), {0}, /*vertexCountHint=*/64};
    REQUIRE(mesh.vertexCount() == 0);
    REQUIRE(mesh.bytes() == 0);
    REQUIRE(mesh.rawData().capacity() >= mesh.layout().getStride() * 64);
}

TEST_CASE("CPUMesh: addVertex writes raw bytes matching the attribute values", "[mesh][cpu]") {
    CPUMesh mesh{VertexLayout{{PositionAttribute{}}}, {0}};

    PositionAttribute p{};
    p.value = glm::vec3{4.0f, 5.0f, 6.0f};
    mesh.addVertex({p});

    REQUIRE(mesh.bytes() == sizeof(glm::vec3));
    glm::vec3 readBack{};
    std::memcpy(&readBack, mesh.rawData().data(), sizeof(glm::vec3));
    REQUIRE(readBack == glm::vec3{4.0f, 5.0f, 6.0f});
}

TEST_CASE("CPUMesh::Hasher: identical meshes hash equally", "[mesh][cpu][hash]") {
    CPUMesh a{posNormalLayout(), {0, 1, 2}};
    CPUMesh b{posNormalLayout(), {0, 1, 2}};

    PositionAttribute p{};
    p.value = glm::vec3{1.0f};
    NormalAttribute n{};

    a.addVertex({p, n});
    b.addVertex({p, n});

    CPUMesh::Hasher h{};
    REQUIRE(h(a) == h(b));
}

TEST_CASE("CPUMesh::Hasher: differs when vertex count differs", "[mesh][cpu][hash]") {
    CPUMesh a{posNormalLayout(), {0}};
    CPUMesh b{posNormalLayout(), {0}};

    PositionAttribute p{};
    NormalAttribute   n{};
    a.addVertex({p, n});
    b.addVertex({p, n}).addVertex({p, n});

    CPUMesh::Hasher h{};
    REQUIRE(h(a) != h(b));
}

TEST_CASE("CPUMesh::Hasher: differs when indices differ", "[mesh][cpu][hash]") {
    CPUMesh a{posNormalLayout(), {0, 1, 2}};
    CPUMesh b{posNormalLayout(), {0, 2, 1}};

    CPUMesh::Hasher h{};
    REQUIRE(h(a) != h(b));
}

TEST_CASE("CPUMesh::Hasher: differs when raw vertex bytes differ", "[mesh][cpu][hash]") {
    CPUMesh a{posNormalLayout(), {0}};
    CPUMesh b{posNormalLayout(), {0}};

    PositionAttribute p1{}; p1.value = glm::vec3{1.0f, 0.0f, 0.0f};
    PositionAttribute p2{}; p2.value = glm::vec3{0.0f, 1.0f, 0.0f};
    NormalAttribute   n{};
    a.addVertex({p1, n});
    b.addVertex({p2, n});

    CPUMesh::Hasher h{};
    REQUIRE(h(a) != h(b));
}
