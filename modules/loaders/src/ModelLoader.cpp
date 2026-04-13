#include <sonnet/loaders/ModelLoader.h>

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/api/render/VertexLayout.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>

#include <stb_image.h>

#include <cstring>
#include <stdexcept>

namespace sonnet::loaders {

using namespace sonnet::api::render;

// ── Shared geometry helpers ────────────────────────────────────────────────────

static VertexLayout pntbtLayout() {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{});
    attrs.insert(TexCoordAttribute{});
    attrs.insert(NormalAttribute{});
    attrs.insert(TangentAttribute{});
    attrs.insert(BiTangentAttribute{});
    return VertexLayout{attrs};
}

static CPUMesh convertMesh(const aiMesh *mesh) {
    std::vector<CPUMesh::Index> indices;
    indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3);
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace &face = mesh->mFaces[f];
        for (unsigned i = 0; i < face.mNumIndices; ++i) {
            indices.push_back(face.mIndices[i]);
        }
    }

    CPUMesh cpuMesh{pntbtLayout(), std::move(indices), mesh->mNumVertices};
    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
        const aiVector3D &p = mesh->mVertices[v];
        const aiVector3D &n = mesh->mNormals[v];
        const glm::vec2 uv = mesh->HasTextureCoords(0)
            ? glm::vec2{mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y}
            : glm::vec2{0.0f};

        glm::vec3 tangent{1.0f, 0.0f, 0.0f};
        glm::vec3 bitangent{0.0f, 1.0f, 0.0f};
        if (mesh->HasTangentsAndBitangents()) {
            const aiVector3D &t  = mesh->mTangents[v];
            const aiVector3D &bt = mesh->mBitangents[v];
            tangent   = {t.x,  t.y,  t.z};
            bitangent = {bt.x, bt.y, bt.z};
        }

        KnownAttributeSet attrs;
        attrs.insert(PositionAttribute{glm::vec3{p.x, p.y, p.z}});
        attrs.insert(TexCoordAttribute{uv});
        attrs.insert(NormalAttribute{glm::vec3{n.x, n.y, n.z}});
        attrs.insert(TangentAttribute{tangent});
        attrs.insert(BiTangentAttribute{bitangent});
        cpuMesh.addVertex(attrs);
    }
    return cpuMesh;
}

// ── ModelLoader::load ──────────────────────────────────────────────────────────

std::vector<CPUMesh> ModelLoader::load(const std::filesystem::path &path) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate      |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        throw std::runtime_error("ModelLoader: failed to load '" + path.string() +
                                 "': " + importer.GetErrorString());
    }

    std::vector<CPUMesh> meshes;
    meshes.reserve(scene->mNumMeshes);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        meshes.push_back(convertMesh(scene->mMeshes[i]));
    }
    return meshes;
}

// ── ModelLoader::loadAll — material extraction ────────────────────────────────

namespace {

// Decode an Assimp embedded texture. Returns nullopt on failure.
static std::optional<CPUTextureBuffer> decodeEmbedded(const aiTexture *tex) {
    if (tex->mHeight == 0) {
        // Compressed image data (PNG/JPEG bytes).
        stbi_set_flip_vertically_on_load(0);
        int w = 0, h = 0, ch = 0;
        auto *data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(tex->pcData),
            static_cast<int>(tex->mWidth),
            &w, &h, &ch, 0);
        if (!data) return std::nullopt;
        const std::size_t n = static_cast<std::size_t>(w) * h * ch;
        std::vector<std::byte> bytes(n);
        std::memcpy(bytes.data(), data, n);
        stbi_image_free(data);
        return CPUTextureBuffer{
            .width    = static_cast<std::uint32_t>(w),
            .height   = static_cast<std::uint32_t>(h),
            .channels = static_cast<std::uint32_t>(ch),
            .texels   = core::Texels{std::move(bytes)},
        };
    }

    // Raw BGRA texels.
    const std::size_t n = static_cast<std::size_t>(tex->mWidth) * tex->mHeight;
    std::vector<std::byte> bytes(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
        const aiTexel &t = tex->pcData[i];
        bytes[i * 4 + 0] = std::byte{t.r};
        bytes[i * 4 + 1] = std::byte{t.g};
        bytes[i * 4 + 2] = std::byte{t.b};
        bytes[i * 4 + 3] = std::byte{t.a};
    }
    return CPUTextureBuffer{
        .width    = tex->mWidth,
        .height   = tex->mHeight,
        .channels = 4,
        .texels   = core::Texels{std::move(bytes)},
    };
}

static MeshTexture extractTex(const aiScene *scene,
                               const aiMaterial *mat,
                               aiTextureType type,
                               unsigned idx,
                               const std::filesystem::path &modelDir,
                               bool srgb) {
    MeshTexture result;
    result.srgb = srgb;
    if (mat->GetTextureCount(type) <= idx) return result;

    aiString aiPath;
    mat->GetTexture(type, idx, &aiPath);

    const aiTexture *embedded = scene->GetEmbeddedTexture(aiPath.C_Str());
    if (embedded) {
        auto decoded = decodeEmbedded(embedded);
        if (decoded) {
            result.embedded = true;
            result.cpuData  = std::move(decoded);
        }
    } else {
        result.path = (modelDir / aiPath.C_Str()).string();
    }
    return result;
}

static MeshMaterial extractMaterial(const aiScene *scene,
                                     const aiMaterial *mat,
                                     const std::filesystem::path &modelDir) {
    MeshMaterial m;
    aiString name;
    mat->Get(AI_MATKEY_NAME, name);
    m.name = name.C_Str();

    // Albedo — try glTF base colour first, fall back to legacy diffuse.
    m.albedo = extractTex(scene, mat, aiTextureType_BASE_COLOR, 0, modelDir, /*srgb=*/true);
    if (!m.albedo.valid())
        m.albedo = extractTex(scene, mat, aiTextureType_DIFFUSE, 0, modelDir, /*srgb=*/true);

    // Tangent-space normal map.
    m.normal = extractTex(scene, mat, aiTextureType_NORMALS, 0, modelDir, /*srgb=*/false);

    // glTF metallicRoughness texture lives in aiTextureType_UNKNOWN slot 0.
    m.orm = extractTex(scene, mat, aiTextureType_UNKNOWN, 0, modelDir, /*srgb=*/false);

    // Emissive map + scalar factor.
    m.emissive = extractTex(scene, mat, aiTextureType_EMISSIVE, 0, modelDir, /*srgb=*/true);
    aiColor3D emissiveColor{0.0f, 0.0f, 0.0f};
    mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor);
    m.emissiveFactor = {emissiveColor.r, emissiveColor.g, emissiveColor.b};

    // Scalar factors.
    aiColor4D color;
    if (AI_SUCCESS == mat->Get(AI_MATKEY_BASE_COLOR, color))
        m.albedoFactor = {color.r, color.g, color.b, color.a};

    float metallic = 1.0f, roughness = 1.0f;
    mat->Get(AI_MATKEY_METALLIC_FACTOR,   metallic);
    mat->Get(AI_MATKEY_ROUGHNESS_FACTOR,  roughness);
    m.metallicFactor  = metallic;
    m.roughnessFactor = roughness;

    return m;
}

} // anonymous namespace

std::vector<LoadedMesh> ModelLoader::loadAll(const std::filesystem::path &path) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate      |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs          // glTF UV (0,0) is top-left; flip to match OpenGL bottom-left
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        throw std::runtime_error("ModelLoader: failed to load '" + path.string() +
                                 "': " + importer.GetErrorString());
    }

    const auto modelDir = path.parent_path();

    std::vector<LoadedMesh> result;
    result.reserve(scene->mNumMeshes);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[i];
        auto cpuMesh = convertMesh(mesh);
        MeshMaterial mat;
        if (mesh->mMaterialIndex < scene->mNumMaterials)
            mat = extractMaterial(scene, scene->mMaterials[mesh->mMaterialIndex], modelDir);
        result.push_back(LoadedMesh{
            std::move(cpuMesh),
            std::move(mat),
            std::string{mesh->mName.C_Str()},
        });
    }
    return result;
}

} // namespace sonnet::loaders
