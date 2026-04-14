#pragma once

#include <sonnet/api/render/CPUMesh.h>
#include <sonnet/api/render/ITexture.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
    bool        alphaMask   = false;  // true when glTF alphaMode == MASK
    float       alphaCutoff = 0.5f;   // discard threshold (glTF default 0.5)
    std::string name;
};

// One mesh + its associated PBR material, as returned by loadAll().
struct LoadedMesh {
    api::render::CPUMesh mesh;
    MeshMaterial         material;
    std::string          name;
};

// One keyframe channel for a single node in an animation clip.
// Keyframe times are in seconds; values are interpolated linearly (positions/scales)
// or spherically (rotations) between the two surrounding keyframes.
struct AnimationChannel {
    std::string                              nodeName;
    std::vector<std::pair<float, glm::vec3>> positions; // (time_s, translation)
    std::vector<std::pair<float, glm::quat>> rotations; // (time_s, quaternion)
    std::vector<std::pair<float, glm::vec3>> scales;    // (time_s, scale)
};

// A single named animation clip extracted from a glTF/GLB file.
struct AnimationClip {
    std::string                   name;
    float                         duration; // seconds
    std::vector<AnimationChannel> channels;
};

// A node in the loaded scene hierarchy.  Mirrors the Assimp aiNode tree so that
// SceneLoader can create one GameObject per node and wire up parent→child
// Transform links.  Animation channels target nodes by name, so empty (mesh-less)
// controller nodes are preserved.
struct LoadedNode {
    std::string       name;
    std::vector<int>  meshIndices; // indices into LoadedModel::meshes
    std::vector<int>  children;    // indices into LoadedModel::nodes
    glm::vec3         localPosition{0.0f};
    glm::quat         localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3         localScale{1.0f};
};

// Return type of loadAll(): full scene hierarchy + meshes + animation clips.
struct LoadedModel {
    std::vector<LoadedMesh>    meshes;
    std::vector<LoadedNode>    nodes;      // full node tree (includes empty nodes)
    int                        rootNode = 0;
    std::vector<AnimationClip> animations;
};

class ModelLoader {
public:
    // Load all meshes from a file. Each Assimp mesh becomes one CPUMesh.
    // Post-processing applied: triangulate, generate normals, calculate tangents.
    // Vertex layout: Position(0) + TexCoord(2) + Normal(3) + Tangent(4) + Bitangent(5).
    [[nodiscard]] static std::vector<api::render::CPUMesh> load(
        const std::filesystem::path &path);

    // Load all meshes, PBR materials, and animation clips from a file.
    // Intended for glTF/GLB. Embedded textures are decoded eagerly.
    // UV coordinates are flipped (aiProcess_FlipUVs) to match OpenGL bottom-left convention.
    [[nodiscard]] static LoadedModel loadAll(
        const std::filesystem::path &path);
};

} // namespace sonnet::loaders
