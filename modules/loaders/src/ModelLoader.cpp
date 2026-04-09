#include <sonnet/loaders/ModelLoader.h>

#include <sonnet/api/render/VertexAttribute.h>
#include <sonnet/api/render/VertexLayout.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <stdexcept>

namespace sonnet::loaders {

using namespace sonnet::api::render;

static VertexLayout pntLayout() {
    KnownAttributeSet attrs;
    attrs.insert(PositionAttribute{});
    attrs.insert(TexCoordAttribute{});
    attrs.insert(NormalAttribute{});
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

    CPUMesh cpuMesh{pntLayout(), std::move(indices), mesh->mNumVertices};
    for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
        const aiVector3D &p = mesh->mVertices[v];
        const aiVector3D &n = mesh->mNormals[v];
        const glm::vec2 uv = mesh->HasTextureCoords(0)
            ? glm::vec2{mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y}
            : glm::vec2{0.0f};

        KnownAttributeSet attrs;
        attrs.insert(PositionAttribute{glm::vec3{p.x, p.y, p.z}});
        attrs.insert(TexCoordAttribute{uv});
        attrs.insert(NormalAttribute{glm::vec3{n.x, n.y, n.z}});
        cpuMesh.addVertex(attrs);
    }
    return cpuMesh;
}

std::vector<CPUMesh> ModelLoader::load(const std::filesystem::path &path) {
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate     |
        aiProcess_GenSmoothNormals|
        aiProcess_CalcTangentSpace|
        aiProcess_FlipUVs
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

} // namespace sonnet::loaders
