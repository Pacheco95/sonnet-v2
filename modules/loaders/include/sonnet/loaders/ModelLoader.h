#pragma once

#include <sonnet/api/render/CPUMesh.h>

#include <filesystem>
#include <vector>

namespace sonnet::loaders {

class ModelLoader {
public:
    // Load all meshes from a file. Each Assimp mesh becomes one CPUMesh.
    // Post-processing applied: triangulate, generate normals, calculate tangents.
    // Vertex layout: Position(0) + TexCoord(2) + Normal(3).
    [[nodiscard]] static std::vector<api::render::CPUMesh> load(
        const std::filesystem::path &path);
};

} // namespace sonnet::loaders
