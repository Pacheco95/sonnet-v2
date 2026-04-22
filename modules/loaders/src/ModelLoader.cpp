#include <sonnet/loaders/ModelLoader.h>

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/api/render/VertexLayout.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace sonnet::loaders {

// ── computeBoundingSphere ──────────────────────────────────────────────────────
// Centroid-based bounding sphere.  PositionAttribute is always location 0 so
// its byte offset is 0 and its type is glm::vec3 (12 bytes).
std::pair<glm::vec3, float>
computeBoundingSphere(const api::render::CPUMesh &mesh) {
    const std::size_t n      = mesh.vertexCount();
    const std::size_t stride = mesh.layout().getStride();
    if (n == 0) return {{0.0f, 0.0f, 0.0f}, 0.0f};

    const auto &raw = mesh.rawData();

    glm::vec3 center{0.0f};
    for (std::size_t i = 0; i < n; ++i) {
        glm::vec3 p;
        std::memcpy(&p, raw.data() + i * stride, sizeof(glm::vec3));
        center += p;
    }
    center /= static_cast<float>(n);

    float maxD2 = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        glm::vec3 p;
        std::memcpy(&p, raw.data() + i * stride, sizeof(glm::vec3));
        const float d2 = glm::dot(p - center, p - center);
        if (d2 > maxD2) maxD2 = d2;
    }
    return {center, std::sqrt(maxD2)};
}

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

static VertexLayout pntbtSkinnedLayout() {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{});
    attrs.insert(TexCoordAttribute{});
    attrs.insert(NormalAttribute{});
    attrs.insert(TangentAttribute{});
    attrs.insert(BiTangentAttribute{});
    attrs.insert(BoneIndexAttribute{});
    attrs.insert(BoneWeightAttribute{});
    return VertexLayout{attrs};
}

// Convert aiMatrix4x4 (row-major) to glm::mat4 (column-major).
static glm::mat4 aiToGLM(const aiMatrix4x4 &m) {
    return glm::mat4{
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4,
    };
}

static CPUMesh convertMesh(const aiMesh *mesh,
                            const std::vector<glm::ivec4> *boneIndices = nullptr,
                            const std::vector<glm::vec4>  *boneWeights = nullptr) {
    std::vector<CPUMesh::Index> indices;
    indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3);
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace &face = mesh->mFaces[f];
        for (unsigned i = 0; i < face.mNumIndices; ++i) {
            indices.push_back(face.mIndices[i]);
        }
    }

    const bool skinned = (boneIndices != nullptr);
    CPUMesh cpuMesh{skinned ? pntbtSkinnedLayout() : pntbtLayout(),
                    std::move(indices), mesh->mNumVertices};

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
        if (skinned) {
            attrs.insert(BoneIndexAttribute{(*boneIndices)[v]});
            attrs.insert(BoneWeightAttribute{(*boneWeights)[v]});
        }
        cpuMesh.addVertex(attrs);
    }
    return cpuMesh;
}

// Build per-vertex bone index/weight arrays from Assimp bone data.
// Returns false if the mesh has no bones.
static bool extractBoneData(const aiMesh *mesh,
                             std::vector<glm::ivec4> &outIndices,
                             std::vector<glm::vec4>  &outWeights,
                             std::vector<BoneInfo>   &outBones) {
    if (!mesh->HasBones()) return false;

    const unsigned numVerts = mesh->mNumVertices;
    outIndices.assign(numVerts, glm::ivec4{0});
    outWeights.assign(numVerts, glm::vec4{0.0f});
    std::vector<int> influence(numVerts, 0); // how many weights assigned so far

    outBones.reserve(mesh->mNumBones);
    for (unsigned bi = 0; bi < mesh->mNumBones; ++bi) {
        const aiBone *bone = mesh->mBones[bi];
        outBones.push_back(BoneInfo{
            bone->mName.C_Str(),
            aiToGLM(bone->mOffsetMatrix),
        });
        for (unsigned wi = 0; wi < bone->mNumWeights; ++wi) {
            const unsigned vid    = bone->mWeights[wi].mVertexId;
            const float    weight = bone->mWeights[wi].mWeight;
            const int      slot   = influence[vid];
            if (slot >= 4) continue; // only keep top 4 influences
            outIndices[vid][slot] = static_cast<int>(bi);
            outWeights[vid][slot] = weight;
            ++influence[vid];
        }
    }

    // Normalize weights so they sum to 1.
    for (unsigned v = 0; v < numVerts; ++v) {
        const float sum = outWeights[v].x + outWeights[v].y +
                          outWeights[v].z + outWeights[v].w;
        if (sum > 0.0f)
            outWeights[v] /= sum;
    }
    return true;
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

    // Alpha mode (glTF MASK = discard-based cutout).
    aiString alphaMode;
    if (AI_SUCCESS == mat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) &&
        std::string_view{alphaMode.C_Str()} == "MASK") {
        m.alphaMask = true;
        mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, m.alphaCutoff);
    }

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

LoadedModel ModelLoader::loadAll(const std::filesystem::path &path) {
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

    // Build the full node hierarchy so SceneLoader can create one GameObject per
    // node, including empty controller/pivot nodes targeted by animation channels.
    LoadedModel result;
    result.meshes.reserve(scene->mNumMeshes);

    // Map from Assimp global mesh index → LoadedModel::meshes index (avoids
    // loading the same mesh twice if multiple nodes share it).
    std::unordered_map<unsigned, int> aiMeshToLoaded;

    std::function<int(const aiNode *)> buildNode = [&](const aiNode *node) -> int {
        // Reserve this node's index before any recursive call that may grow the vector.
        const int nodeIdx = static_cast<int>(result.nodes.size());
        result.nodes.emplace_back();

        {
            LoadedNode &ln = result.nodes[nodeIdx];
            ln.name = node->mName.C_Str();

            // Decompose the node's local transform.
            aiVector3D pos, scale;
            aiQuaternion rot;
            node->mTransformation.Decompose(scale, rot, pos);
            ln.localPosition = {pos.x, pos.y, pos.z};
            // aiQuaternion is (w,x,y,z); glm::quat constructor is (w,x,y,z).
            ln.localRotation = glm::quat{rot.w, rot.x, rot.y, rot.z};
            ln.localScale    = {scale.x, scale.y, scale.z};

            // Load meshes referenced by this node.
            for (unsigned mi = 0; mi < node->mNumMeshes; ++mi) {
                const unsigned aiIdx = node->mMeshes[mi];
                auto it = aiMeshToLoaded.find(aiIdx);
                if (it == aiMeshToLoaded.end()) {
                    const aiMesh *mesh = scene->mMeshes[aiIdx];

                    // Extract bone data first (if any) so the mesh gets the
                    // correct vertex layout (PNTBT vs PNTBT+skin).
                    std::vector<glm::ivec4> boneIdx;
                    std::vector<glm::vec4>  boneWgt;
                    std::vector<BoneInfo>   bones;
                    const bool hasSkin = extractBoneData(mesh, boneIdx, boneWgt, bones);

                    auto cpuMesh = hasSkin
                        ? convertMesh(mesh, &boneIdx, &boneWgt)
                        : convertMesh(mesh);

                    MeshMaterial mat;
                    if (mesh->mMaterialIndex < scene->mNumMaterials)
                        mat = extractMaterial(scene,
                                              scene->mMaterials[mesh->mMaterialIndex],
                                              modelDir);
                    auto [bc, br] = computeBoundingSphere(cpuMesh);
                    const int loadedIdx = static_cast<int>(result.meshes.size());
                    result.meshes.push_back(LoadedMesh{
                        std::move(cpuMesh),
                        std::move(mat),
                        std::string{node->mName.C_Str()},
                        bc,
                        br,
                        hasSkin,
                        std::move(bones),
                    });
                    aiMeshToLoaded[aiIdx] = loadedIdx;
                    it = aiMeshToLoaded.find(aiIdx);
                }
                result.nodes[nodeIdx].meshIndices.push_back(it->second);
            }
        }

        // Recurse into children after setting up this node.  Recursive calls may
        // reallocate result.nodes, so always access by index, never by pointer.
        for (unsigned ci = 0; ci < node->mNumChildren; ++ci) {
            const int childIdx = buildNode(node->mChildren[ci]);
            result.nodes[nodeIdx].children.push_back(childIdx);
        }
        return nodeIdx;
    };
    result.rootNode = buildNode(scene->mRootNode);

    // ── Animation clips ───────────────────────────────────────────────────────
    for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation *anim = scene->mAnimations[a];
        const double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

        AnimationClip clip;
        clip.name     = anim->mName.C_Str();
        clip.duration = static_cast<float>(anim->mDuration / tps);

        for (unsigned c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim *ch = anim->mChannels[c];
            AnimationChannel channel;
            channel.nodeName = ch->mNodeName.C_Str();

            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const auto &key = ch->mPositionKeys[k];
                channel.positions.push_back({
                    static_cast<float>(key.mTime / tps),
                    {key.mValue.x, key.mValue.y, key.mValue.z},
                });
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const auto &key = ch->mRotationKeys[k];
                // aiQuaternion is (w,x,y,z); glm::quat constructor is (w,x,y,z)
                channel.rotations.push_back({
                    static_cast<float>(key.mTime / tps),
                    glm::quat{key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z},
                });
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const auto &key = ch->mScalingKeys[k];
                channel.scales.push_back({
                    static_cast<float>(key.mTime / tps),
                    {key.mValue.x, key.mValue.y, key.mValue.z},
                });
            }
            clip.channels.push_back(std::move(channel));
        }
        result.animations.push_back(std::move(clip));
    }

    return result;
}

} // namespace sonnet::loaders
