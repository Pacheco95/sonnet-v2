#pragma once

#include <sonnet/api/render/Light.h>
#include <sonnet/core/Types.h>

#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations — callers only need the full types when constructing.
namespace sonnet::world        { class Scene; class GameObject; }
namespace sonnet::renderer::frontend { class Renderer; }

namespace sonnet::scene {

// Result of a scene load: scene-graph objects, named material templates,
// and lights parsed from objects with a "light" component.
struct LoadedScene {
    std::unordered_map<std::string, world::GameObject *>           objects;
    std::unordered_map<std::string, core::MaterialTemplateHandle>  materials;
    std::vector<api::render::DirectionalLight>                     directionalLights;
    std::vector<api::render::PointLight>                           pointLights;
};

// Loads a scene from a JSON file or string.
//
// Runtime textures that cannot be created from files (e.g. render-target
// outputs such as shadow maps) must be registered before calling load() so
// the scene file can reference them by name.
//
// JSON schema
// ──────────────────────────────────────────────────────────────
// {
//   "assets": {
//     "shaders": {
//       "name": { "vert": "shaders/file.vert", "frag": "shaders/file.frag" }
//     },
//     "materials": {
//       "name": {
//         "shader": "shaderName",
//         "defaultValues": { "uUniform": 0.005 }  // float, [x,y], [x,y,z], [x,y,z,w]
//       }
//     },
//     "meshes": {
//       "name": "path/to/file.obj",
//       "name": { "primitive": "box",  "size": [w, h, d] },
//       "name": { "primitive": "quad", "size": [w, h]    }
//     },
//     "textures": {
//       "name": "path/to/texture.png",
//       "name": { "color": [r, g, b] }     // solid 1x1, values 0-255
//     }
//   },
//   "objects": [
//     {
//       "name":     "ObjectName",
//       "parent":   "ParentName",           // optional
//       "position": [x, y, z],              // optional, default [0,0,0]
//       "rotation": [x, y, z, w],           // optional quat, default identity
//       "scale":    [x, y, z],              // optional, default [1,1,1]
//       "camera": { "fov": 60, "near": 0.1, "far": 200 }, // optional
//       "render": {                          // optional
//         "mesh":     "meshName",
//         "material": "materialName",
//         "textures": { "uUniform": "textureName" }
//       }
//     }
//   ]
// }
class SceneLoader {
public:
    // Pre-register a named texture for scene file references (e.g. render target outputs).
    void registerTexture(const std::string &name, core::GPUTextureHandle handle);

    // Load from a JSON file. Relative asset paths are resolved from assetsDir.
    LoadedScene load(const std::string &sceneFile,
                     const std::string &assetsDir,
                     world::Scene &scene,
                     renderer::frontend::Renderer &renderer);

    // Load from a JSON string. When renderer is null, asset loading and render
    // component assignment are skipped — useful for unit tests.
    LoadedScene loadFromString(const std::string &json,
                               const std::string &assetsDir,
                               world::Scene &scene,
                               renderer::frontend::Renderer *renderer = nullptr);

private:
    std::unordered_map<std::string, core::GPUTextureHandle> m_textures;
};

} // namespace sonnet::scene
