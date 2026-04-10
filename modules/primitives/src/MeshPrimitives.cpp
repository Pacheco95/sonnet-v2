#include <sonnet/primitives/MeshPrimitives.h>

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/api/render/VertexLayout.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace sonnet::primitives {

using namespace sonnet::api::render;

// ── Layout builders ───────────────────────────────────────────────────────────

// Position + TexCoord + Normal (quads, spheres — no tangents needed)
static VertexLayout pntLayout() {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{});
    attrs.insert(TexCoordAttribute{});
    attrs.insert(NormalAttribute{});
    return VertexLayout{attrs};
}

// Position + TexCoord + Normal + Tangent + BiTangent (boxes, OBJ models)
static VertexLayout pntbtLayout() {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{});
    attrs.insert(TexCoordAttribute{});
    attrs.insert(NormalAttribute{});
    attrs.insert(TangentAttribute{});
    attrs.insert(BiTangentAttribute{});
    return VertexLayout{attrs};
}

static void addVertex(CPUMesh &mesh,
                      const glm::vec3 &pos,
                      const glm::vec2 &uv,
                      const glm::vec3 &normal) {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{pos});
    attrs.insert(TexCoordAttribute{uv});
    attrs.insert(NormalAttribute{normal});
    mesh.addVertex(attrs);
}

static void addVertex(CPUMesh &mesh,
                      const glm::vec3 &pos,
                      const glm::vec2 &uv,
                      const glm::vec3 &normal,
                      const glm::vec3 &tangent,
                      const glm::vec3 &bitangent) {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{pos});
    attrs.insert(TexCoordAttribute{uv});
    attrs.insert(NormalAttribute{normal});
    attrs.insert(TangentAttribute{tangent});
    attrs.insert(BiTangentAttribute{bitangent});
    mesh.addVertex(attrs);
}

// ── Box ───────────────────────────────────────────────────────────────────────

api::render::CPUMesh makeBox(glm::vec3 size) {
    const glm::vec3 h = size * 0.5f;

    struct FaceVert { glm::vec3 pos; glm::vec2 uv; };
    struct Face {
        FaceVert   v[4];
        glm::vec3  normal;
        // Tangent = dPosition/dU, BiTangent = dPosition/dV (UV-space axes in world space).
        // Derived from the vertex positions at UV corners (0,0)→(1,0) and (0,0)→(0,1).
        glm::vec3  tangent;
        glm::vec3  bitangent;
    };

    // clang-format off
    const Face faces[] = {
        // +X  normal=(+1,0,0)  UV: U→+Z, V→+Y
        { {{{+h.x,-h.y,-h.z},{0,0}}, {{+h.x,+h.y,-h.z},{0,1}}, {{+h.x,+h.y,+h.z},{1,1}}, {{+h.x,-h.y,+h.z},{1,0}}}, {+1,0,0}, {0,0,+1}, {0,+1,0} },
        // -X  normal=(-1,0,0)  UV: U→-Z, V→+Y
        { {{{-h.x,-h.y,+h.z},{0,0}}, {{-h.x,+h.y,+h.z},{0,1}}, {{-h.x,+h.y,-h.z},{1,1}}, {{-h.x,-h.y,-h.z},{1,0}}}, {-1,0,0}, {0,0,-1}, {0,+1,0} },
        // +Y  normal=(0,+1,0)  UV: U→+X, V→+Z
        { {{{-h.x,+h.y,-h.z},{0,0}}, {{-h.x,+h.y,+h.z},{0,1}}, {{+h.x,+h.y,+h.z},{1,1}}, {{+h.x,+h.y,-h.z},{1,0}}}, {0,+1,0}, {+1,0,0}, {0,0,+1} },
        // -Y  normal=(0,-1,0)  UV: U→+X, V→-Z
        { {{{-h.x,-h.y,+h.z},{0,0}}, {{-h.x,-h.y,-h.z},{0,1}}, {{+h.x,-h.y,-h.z},{1,1}}, {{+h.x,-h.y,+h.z},{1,0}}}, {0,-1,0}, {+1,0,0}, {0,0,-1} },
        // +Z  normal=(0,0,+1)  UV: U→+X, V→+Y
        { {{{-h.x,-h.y,+h.z},{0,0}}, {{+h.x,-h.y,+h.z},{1,0}}, {{+h.x,+h.y,+h.z},{1,1}}, {{-h.x,+h.y,+h.z},{0,1}}}, {0,0,+1}, {+1,0,0}, {0,+1,0} },
        // -Z  normal=(0,0,-1)  UV: U→-X, V→+Y
        { {{{+h.x,-h.y,-h.z},{0,0}}, {{-h.x,-h.y,-h.z},{1,0}}, {{-h.x,+h.y,-h.z},{1,1}}, {{+h.x,+h.y,-h.z},{0,1}}}, {0,0,-1}, {-1,0,0}, {0,+1,0} },
    };
    // clang-format on

    std::vector<CPUMesh::Index> indices;
    indices.reserve(36);
    for (std::uint32_t f = 0; f < 6; ++f) {
        const std::uint32_t base = f * 4;
        indices.insert(indices.end(), {base+0,base+1,base+2, base+0,base+2,base+3});
    }

    CPUMesh mesh{pntbtLayout(), std::move(indices), 24};
    for (const auto &face : faces) {
        for (const auto &v : face.v) {
            addVertex(mesh, v.pos, v.uv, face.normal, face.tangent, face.bitangent);
        }
    }
    return mesh;
}

// ── UVSphere ──────────────────────────────────────────────────────────────────

api::render::CPUMesh makeUVSphere(int segmentsX, int segmentsY, bool smooth) {
    const auto segX = static_cast<std::uint32_t>(std::max(segmentsX, 3));
    const auto segY = static_cast<std::uint32_t>(std::max(segmentsY, 2));

    static constexpr float PI = std::numbers::pi_v<float>;

    if (smooth) {
        // Shared vertices — smooth normals.
        const std::size_t vertCount = static_cast<std::size_t>((segX + 1) * (segY + 1));
        std::vector<CPUMesh::Index> indices;
        indices.reserve(segX * segY * 6);

        const std::uint32_t stride = segX + 1;
        for (std::uint32_t y = 0; y < segY; ++y) {
            for (std::uint32_t x = 0; x < segX; ++x) {
                const std::uint32_t i0 = y * stride + x;
                const std::uint32_t i1 = i0 + 1;
                const std::uint32_t i2 = i0 + stride;
                const std::uint32_t i3 = i2 + 1;
                indices.insert(indices.end(), {i0,i1,i2, i1,i3,i2});
            }
        }

        CPUMesh mesh{pntLayout(), std::move(indices), vertCount};
        for (std::uint32_t y = 0; y <= segY; ++y) {
            const float v   = static_cast<float>(y) / static_cast<float>(segY);
            const float phi = v * PI;
            for (std::uint32_t x = 0; x <= segX; ++x) {
                const float u     = static_cast<float>(x) / static_cast<float>(segX);
                const float theta = u * 2.0f * PI;
                const glm::vec3 n = glm::normalize(glm::vec3{
                    std::cos(theta) * std::sin(phi),
                    std::cos(phi),
                    std::sin(theta) * std::sin(phi),
                });
                addVertex(mesh, n, {u, 1.0f - v}, n);
            }
        }
        return mesh;
    } else {
        // Duplicated vertices — flat normals.
        struct V { glm::vec3 pos, normal; glm::vec2 uv; };
        std::vector<V> verts;
        std::vector<CPUMesh::Index> indices;
        verts.reserve(segX * segY * 6);
        indices.reserve(segX * segY * 6);

        for (std::uint32_t y = 0; y < segY; ++y) {
            const float v0   = static_cast<float>(y)     / static_cast<float>(segY);
            const float v1   = static_cast<float>(y + 1) / static_cast<float>(segY);
            const float phi0 = v0 * PI, phi1 = v1 * PI;

            for (std::uint32_t x = 0; x < segX; ++x) {
                const float u0     = static_cast<float>(x)     / static_cast<float>(segX);
                const float u1     = static_cast<float>(x + 1) / static_cast<float>(segX);
                const float theta0 = u0 * 2.0f * PI, theta1 = u1 * 2.0f * PI;

                auto sph = [](float phi, float theta) {
                    return glm::vec3{std::cos(theta)*std::sin(phi),
                                     std::cos(phi),
                                     std::sin(theta)*std::sin(phi)};
                };
                const glm::vec3 p00 = sph(phi0,theta0), p10 = sph(phi0,theta1);
                const glm::vec3 p01 = sph(phi1,theta0), p11 = sph(phi1,theta1);

                const glm::vec3 n0 = glm::normalize(glm::cross(p10-p00, p01-p00));
                const glm::vec3 n1 = glm::normalize(glm::cross(p11-p10, p01-p10));

                const auto base = static_cast<CPUMesh::Index>(verts.size());
                verts.push_back({p00, n0, {u0, 1.0f-v0}});
                verts.push_back({p10, n0, {u1, 1.0f-v0}});
                verts.push_back({p01, n0, {u0, 1.0f-v1}});
                verts.push_back({p10, n1, {u1, 1.0f-v0}});
                verts.push_back({p11, n1, {u1, 1.0f-v1}});
                verts.push_back({p01, n1, {u0, 1.0f-v1}});
                indices.insert(indices.end(), {base,base+1,base+2, base+3,base+4,base+5});
            }
        }

        CPUMesh mesh{pntLayout(), std::move(indices), verts.size()};
        for (const auto &v : verts) {
            addVertex(mesh, v.pos, v.uv, v.normal);
        }
        return mesh;
    }
}

// ── Quad ──────────────────────────────────────────────────────────────────────

api::render::CPUMesh makeQuad(glm::vec2 size) {
    const glm::vec2 h = size * 0.5f;
    const glm::vec3 n{0.0f, 0.0f, 1.0f};

    std::vector<CPUMesh::Index> indices{0, 1, 2, 0, 2, 3};
    CPUMesh mesh{pntLayout(), std::move(indices), 4};

    addVertex(mesh, {-h.x, -h.y, 0.0f}, {0.0f, 0.0f}, n);
    addVertex(mesh, { h.x, -h.y, 0.0f}, {1.0f, 0.0f}, n);
    addVertex(mesh, { h.x,  h.y, 0.0f}, {1.0f, 1.0f}, n);
    addVertex(mesh, {-h.x,  h.y, 0.0f}, {0.0f, 1.0f}, n);

    return mesh;
}

} // namespace sonnet::primitives
