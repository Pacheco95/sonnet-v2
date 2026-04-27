#include <sonnet/loaders/ModelLoader.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using sonnet::loaders::ModelLoader;
using sonnet::loaders::LoadedModel;
using Catch::Matchers::WithinAbs;

namespace {

namespace fs = std::filesystem;

fs::path fixture(const fs::path &relative) {
    return fs::path{SONNET_LOADERS_FIXTURES_DIR} / relative;
}

fs::path makeTempDir() {
    static std::atomic<unsigned> counter{0};
    auto base = fs::temp_directory_path() / "sonnet_model_loader_tests";
    fs::create_directories(base);
    auto dir = base / ("run_" + std::to_string(::getpid()) + "_" +
                       std::to_string(counter.fetch_add(1)));
    fs::create_directories(dir);
    return dir;
}

} // namespace

// ── Errors ──────────────────────────────────────────────────────────────────

TEST_CASE("ModelLoader::load on missing file throws runtime_error with the path",
         "[loaders][model][error]") {
    try {
        (void)ModelLoader::load("/tmp/sonnet_model_loader_tests/__nope__.gltf");
        FAIL("expected runtime_error");
    } catch (const std::runtime_error &e) {
        std::string what = e.what();
        REQUIRE(what.find("ModelLoader") != std::string::npos);
    }
}

TEST_CASE("ModelLoader::loadAll on garbage input throws", "[loaders][model][error]") {
    auto dir = makeTempDir();
    auto path = dir / "garbage.gltf";
    {
        std::ofstream out{path};
        out << "this is not a gltf";
    }
    REQUIRE_THROWS_AS(ModelLoader::loadAll(path), std::runtime_error);
}

// ── load (legacy) ───────────────────────────────────────────────────────────

TEST_CASE("ModelLoader::load on Triangle.gltf produces one CPUMesh with PNTBT layout",
         "[loaders][model][gltf]") {
    auto meshes = ModelLoader::load(fixture("Triangle/Triangle.gltf"));
    REQUIRE(meshes.size() == 1);

    const auto &m = meshes[0];
    REQUIRE(m.vertexCount() == 3);

    // PNTBT layout: Position(12) + TexCoord(8) + Normal(12) + Tangent(12) +
    // BiTangent(12) = 56 bytes per vertex.
    REQUIRE(m.layout().getStride() == 56);
    REQUIRE(m.rawData().size() == 56u * 3u);
}

TEST_CASE("ModelLoader::load on TriangleWithoutIndices still loads geometry",
         "[loaders][model][gltf]") {
    auto meshes = ModelLoader::load(fixture("TriangleWithoutIndices/TriangleWithoutIndices.gltf"));
    REQUIRE(meshes.size() == 1);
    REQUIRE(meshes[0].vertexCount() == 3);
}

// ── loadAll: static mesh + bounding sphere + node tree ──────────────────────

TEST_CASE("ModelLoader::loadAll on Triangle populates meshes, nodes, bounds",
         "[loaders][model][gltf]") {
    LoadedModel m = ModelLoader::loadAll(fixture("Triangle/Triangle.gltf"));

    REQUIRE(m.meshes.size() == 1);
    REQUIRE_FALSE(m.meshes[0].hasSkin);
    REQUIRE(m.meshes[0].mesh.vertexCount() == 3);
    REQUIRE(m.meshes[0].boundsRadius > 0.0f);

    REQUIRE_FALSE(m.nodes.empty());
    REQUIRE(m.rootNode >= 0);
    REQUIRE(m.rootNode < static_cast<int>(m.nodes.size()));

    REQUIRE(m.animations.empty());
}

// ── Material: external texture path ─────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll on BoxTextured.gltf records external texture path",
         "[loaders][model][gltf][material]") {
    LoadedModel m = ModelLoader::loadAll(fixture("BoxTextured/BoxTextured.gltf"));
    REQUIRE_FALSE(m.meshes.empty());

    const auto &mat = m.meshes[0].material;
    REQUIRE(mat.albedo.valid());
    REQUIRE_FALSE(mat.albedo.embedded);
    REQUIRE(mat.albedo.srgb);
    REQUIRE(mat.albedo.path.find("CesiumLogoFlat.png") != std::string::npos);
    // The path is resolved relative to the model dir.
    REQUIRE(fs::exists(mat.albedo.path));
}

// ── Material: embedded texture (GLB) ────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll on BoxTextured.glb decodes embedded texture",
         "[loaders][model][gltf][material]") {
    LoadedModel m = ModelLoader::loadAll(fixture("BoxTextured/BoxTextured.glb"));
    REQUIRE_FALSE(m.meshes.empty());

    const auto &mat = m.meshes[0].material;
    REQUIRE(mat.albedo.valid());
    REQUIRE(mat.albedo.embedded);
    REQUIRE(mat.albedo.cpuData.has_value());
    REQUIRE(mat.albedo.cpuData->width  > 0u);
    REQUIRE(mat.albedo.cpuData->height > 0u);
    REQUIRE((mat.albedo.cpuData->channels == 3u || mat.albedo.cpuData->channels == 4u));
    // The raw-BGRA branch in decodeEmbedded (tex->mHeight != 0) is unreachable
    // from glTF — Assimp always synthesises a height==0 compressed aiTexture
    // for embedded glTF images. That branch is intentionally not covered.
}

// ── Material: scalar factor defaults ────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll preserves default scalar factors when asset omits them",
         "[loaders][model][gltf][material]") {
    // Triangle has no material at all → factors stay at struct defaults.
    LoadedModel m = ModelLoader::loadAll(fixture("Triangle/Triangle.gltf"));
    REQUIRE_FALSE(m.meshes.empty());

    const auto &mat = m.meshes[0].material;
    REQUIRE_FALSE(mat.alphaMask);
    REQUIRE_THAT(mat.alphaCutoff,    WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(mat.metallicFactor, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(mat.roughnessFactor, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(mat.emissiveFactor.x, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(mat.emissiveFactor.y, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(mat.emissiveFactor.z, WithinAbs(0.0f, 1e-6f));
}

// ── Animation clip: TRS channels ────────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll on BoxAnimated produces animation channels in seconds",
         "[loaders][model][gltf][animation]") {
    LoadedModel m = ModelLoader::loadAll(fixture("BoxAnimated/BoxAnimated.gltf"));
    REQUIRE_FALSE(m.animations.empty());

    const auto &clip = m.animations[0];
    REQUIRE(clip.duration > 0.0f);
    REQUIRE(clip.duration < 10.0f); // catches ticksPerSecond mis-conversion
    REQUIRE_FALSE(clip.channels.empty());

    // At least one channel must carry keyframes of some kind. Assimp may
    // split position vs rotation across channels, so don't assume all three
    // live on the same channel.
    bool sawAnyKeyframes = false;
    for (const auto &ch : clip.channels) {
        if (!ch.positions.empty() || !ch.rotations.empty() || !ch.scales.empty()) {
            sawAnyKeyframes = true;
            REQUIRE_FALSE(ch.nodeName.empty());
            break;
        }
    }
    REQUIRE(sawAnyKeyframes);
}

// ── Skinning ────────────────────────────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll on RiggedSimple extracts bones with normalised weights",
         "[loaders][model][gltf][skin]") {
    LoadedModel m = ModelLoader::loadAll(fixture("RiggedSimple/RiggedSimple.gltf"));

    // Find the skinned mesh — RiggedSimple has one.
    const sonnet::loaders::LoadedMesh *skinned = nullptr;
    for (const auto &lm : m.meshes) {
        if (lm.hasSkin) { skinned = &lm; break; }
    }
    REQUIRE(skinned != nullptr);
    REQUIRE_FALSE(skinned->bones.empty());

    // Skinned layout adds BoneIndex (16B int4) + BoneWeight (16B float4) on
    // top of the 56B PNTBT block → 88B per vertex.
    REQUIRE(skinned->mesh.layout().getStride() == 88);

    // Pull the first vertex's bone weights out of the raw buffer and verify
    // they sum to ~1.0 (extractBoneData's normalisation pass).
    const auto &raw = skinned->mesh.rawData();
    REQUIRE(raw.size() >= 88);
    glm::vec4 weights{0.0f};
    // BoneWeight is the last attribute in the pntbtSkinnedLayout — at byte
    // offset 72 (Position 0..12, TexCoord 12..20, Normal 20..32,
    // Tangent 32..44, BiTangent 44..56, BoneIndex 56..72, BoneWeight 72..88).
    std::memcpy(&weights, raw.data() + 72, sizeof(glm::vec4));
    const float sum = weights.x + weights.y + weights.z + weights.w;
    REQUIRE_THAT(sum, WithinAbs(1.0f, 1e-3f));
}

// ── Node hierarchy ──────────────────────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll preserves the node hierarchy including empty controllers",
         "[loaders][model][gltf][hierarchy]") {
    LoadedModel m = ModelLoader::loadAll(fixture("RiggedSimple/RiggedSimple.gltf"));

    REQUIRE(m.nodes.size() >= 3);
    // Some node must have at least one child (otherwise the recursive build
    // path is unexercised).
    bool anyParent = false;
    for (const auto &n : m.nodes) {
        if (!n.children.empty()) { anyParent = true; break; }
    }
    REQUIRE(anyParent);

    // At least one node must be empty (mesh-less) — a controller / pivot
    // node. RiggedSimple has bone joints that fit this profile.
    bool anyEmpty = false;
    for (const auto &n : m.nodes) {
        if (n.meshIndices.empty()) { anyEmpty = true; break; }
    }
    REQUIRE(anyEmpty);
}

// ── Mesh dedup ──────────────────────────────────────────────────────────────

TEST_CASE("ModelLoader::loadAll deduplicates a glTF mesh referenced by multiple nodes",
         "[loaders][model][gltf][hierarchy]") {
    // Tiny inline glTF: two nodes (siblings under root) both reference mesh 0.
    // Geometry is a single triangle stored as a base64 buffer.
    //   positions: (0,0,0), (1,0,0), (0,1,0)
    //   indices:   0, 1, 2  (uint16)
    // Buffer layout: 36 bytes positions then 6 bytes indices = 42 bytes.
    //
    // Hand-encoded base64 of those exact bytes:
    static constexpr const char *kInlineGltf = R"({
    "asset": { "version": "2.0" },
    "scene": 0,
    "scenes": [{ "nodes": [0] }],
    "nodes": [
        { "children": [1, 2] },
        { "mesh": 0 },
        { "mesh": 0 }
    ],
    "meshes": [{
        "primitives": [{
            "attributes": { "POSITION": 0 },
            "indices": 1
        }]
    }],
    "accessors": [
        { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
          "min": [0,0,0], "max": [1,1,0] },
        { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
    ],
    "bufferViews": [
        { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 },
        { "buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963 }
    ],
    "buffers": [{
        "byteLength": 42,
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"
    }]
})";

    auto dir = makeTempDir();
    auto path = dir / "shared_mesh.gltf";
    {
        std::ofstream out{path};
        out << kInlineGltf;
    }

    LoadedModel m = ModelLoader::loadAll(path);
    // Two nodes pointed at the same mesh index — aiMeshToLoaded must coalesce
    // them so meshes.size() == 1, while the node tree retains both refs.
    REQUIRE(m.meshes.size() == 1);
    REQUIRE(m.nodes.size() >= 3);
}
