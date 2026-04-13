#pragma once

#include <sonnet/api/render/CPUMesh.h>
#include <sonnet/api/render/ITexture.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace sonnet::loaders {

// A single texture extracted from a model file. Either a file path (external)
// or pre-decoded CPU pixels (embedded in a GLB binary).
struct MeshTexture {
    std::string                              path;    // absolute path; empty if embedded/missing
    std::optional<api::render::CPUTextureBuffer> cpuData; // decoded pixels (embedded GLB)
    bool                                     embedded = false;
    bool                                     srgb     = true;

    [[nodiscard]] bool valid() const { return !path.empty() || embedded; }
};

// PBR material extracted from a model's aiMaterial.
struct MeshMaterial {
    MeshTexture albedo;              // base colour (sRGB)
    MeshTexture normal;              // tangent-space normal map (linear)
    MeshTexture orm;                 // G=roughness, B=metallic per glTF convention (linear)
    MeshTexture emissive;            // emissive/glow map (sRGB); valid() false if absent
    glm::vec4   albedoFactor{1.0f};
    float       metallicFactor  = 1.0f;
    float       roughnessFactor = 1.0f;
    glm::vec3   emissiveFactor{0.0f}; // vec3 multiplier; [0,0,0] = no emission
    std::string name;
};

// One mesh + its associated PBR material, as returned by loadAll().
struct LoadedMesh {
    api::render::CPUMesh mesh;
    MeshMaterial         material;
    std::string          name;
};

class ModelLoader {
public:
    // Load all meshes from a file. Each Assimp mesh becomes one CPUMesh.
    // Post-processing applied: triangulate, generate normals, calculate tangents.
    // Vertex layout: Position(0) + TexCoord(2) + Normal(3) + Tangent(4) + Bitangent(5).
    [[nodiscard]] static std::vector<api::render::CPUMesh> load(
        const std::filesystem::path &path);

    // Load all meshes and their PBR materials. Intended for glTF/GLB files.
    // Embedded textures (GLB) are decoded eagerly; external textures store their paths.
    // UV coordinates are flipped (aiProcess_FlipUVs) to match OpenGL bottom-left convention.
    [[nodiscard]] static std::vector<LoadedMesh> loadAll(
        const std::filesystem::path &path);
};

} // namespace sonnet::loaders
