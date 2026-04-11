#include <sonnet/scene/SceneLoader.h>

#include <sonnet/api/render/Material.h>
#include <sonnet/loaders/ModelLoader.h>
#include <sonnet/loaders/ShaderLoader.h>
#include <sonnet/loaders/TextureLoader.h>
#include <sonnet/primitives/MeshPrimitives.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/world/CameraComponent.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Scene.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace sonnet::scene {

using namespace api::render;
using json = nlohmann::json;

// ── Registration ───────────────────────────────────────────────────────────────

void SceneLoader::registerTexture(const std::string &name, core::GPUTextureHandle handle) {
    m_textures[name] = handle;
}

// ── Internal helpers ───────────────────────────────────────────────────────────

namespace {

core::GPUMeshHandle loadMesh(const json &spec,
                              const std::string &assetsDir,
                              renderer::frontend::Renderer &renderer) {
    if (spec.is_string()) {
        const std::string path = assetsDir + "/" + spec.get<std::string>();
        auto meshes = loaders::ModelLoader::load(path);
        if (meshes.empty())
            throw std::runtime_error("SceneLoader: no meshes in " + path);
        return renderer.createMesh(meshes[0]);
    }

    const std::string prim = spec.at("primitive");
    if (prim == "box") {
        auto s = spec.at("size");
        return renderer.createMesh(primitives::makeBox({s[0], s[1], s[2]}));
    }
    if (prim == "quad") {
        auto s = spec.at("size");
        return renderer.createMesh(primitives::makeQuad({s[0].get<float>(), s[1].get<float>()}));
    }
    if (prim == "sphere") {
        const int segX = spec.value("segmentsX", 32);
        const int segY = spec.value("segmentsY", 16);
        return renderer.createMesh(primitives::makeUVSphere(segX, segY, /*smooth=*/true));
    }
    throw std::runtime_error("SceneLoader: unknown primitive '" + prim + "'");
}

core::GPUTextureHandle loadTexture(const json &spec,
                                    const std::string &assetsDir,
                                    renderer::frontend::Renderer &renderer) {
    // Optional "srgb": false marks data textures (normal maps, ORM) as linear.
    // Default is true (sRGB) for colour/albedo textures.
    const bool srgb = spec.is_object() ? spec.value("srgb", true) : true;
    const ColorSpace colorSpace = srgb ? ColorSpace::sRGB : ColorSpace::Linear;

    // File path: either a bare string or { "file": "path", "srgb": false }
    if (spec.is_string() || (spec.is_object() && spec.contains("file"))) {
        const std::string filePath = spec.is_string()
            ? spec.get<std::string>()
            : spec["file"].get<std::string>();
        const std::string path = assetsDir + "/" + filePath;
        auto cpuTex = loaders::TextureLoader::load(path);
        const auto fmt = cpuTex.channels == 4 ? TextureFormat::RGBA8 : TextureFormat::RGB8;
        return renderer.createTexture(
            TextureDesc{
                .size       = {cpuTex.width, cpuTex.height},
                .format     = fmt,
                .colorSpace = colorSpace,
            }, {}, cpuTex);
    }

    // Solid 1x1 color: { "color": [r, g, b] }  (values 0-255)
    auto c = spec.at("color");
    const std::byte pixel[] = {
        std::byte{c[0].get<std::uint8_t>()},
        std::byte{c[1].get<std::uint8_t>()},
        std::byte{c[2].get<std::uint8_t>()},
    };
    return renderer.createTexture(
        TextureDesc{
            .size       = {1, 1},
            .format     = TextureFormat::RGB8,
            .colorSpace = colorSpace,
            .useMipmaps = false,
        },
        {},
        CPUTextureBuffer{
            .width    = 1,
            .height   = 1,
            .channels = 3,
            .texels   = core::Texels{pixel, std::size(pixel)},
        });
}

// Parse a JSON value into a UniformValue. Supports float, vec2, vec3, vec4.
core::UniformValue parseUniformValue(const json &v) {
    if (v.is_number())
        return v.get<float>();
    if (v.is_array()) {
        if (v.size() == 2)
            return glm::vec2{v[0].get<float>(), v[1].get<float>()};
        if (v.size() == 3)
            return glm::vec3{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
        if (v.size() == 4)
            return glm::vec4{v[0].get<float>(), v[1].get<float>(),
                             v[2].get<float>(), v[3].get<float>()};
    }
    throw std::runtime_error("SceneLoader: unsupported defaultValue type in material");
}

} // anonymous namespace

// ── Public load interface ──────────────────────────────────────────────────────

LoadedScene SceneLoader::load(const std::string &sceneFile,
                               const std::string &assetsDir,
                               world::Scene &scene,
                               renderer::frontend::Renderer &renderer) {
    std::ifstream f{sceneFile};
    if (!f)
        throw std::runtime_error("SceneLoader: cannot open '" + sceneFile + "'");
    const std::string content{std::istreambuf_iterator<char>{f},
                               std::istreambuf_iterator<char>{}};
    return loadFromString(content, assetsDir, scene, &renderer);
}

LoadedScene SceneLoader::loadFromString(const std::string &jsonStr,
                                         const std::string &assetsDir,
                                         world::Scene &scene,
                                         renderer::frontend::Renderer *renderer) {
    const json doc = json::parse(jsonStr);
    LoadedScene result;

    // ── Asset loading (skipped when renderer is null) ──────────────────────────
    std::unordered_map<std::string, core::GPUMeshHandle>             meshes;
    std::unordered_map<std::string, core::GPUTextureHandle>          textures = m_textures;
    std::unordered_map<std::string, core::ShaderHandle>              shaders;
    std::unordered_map<std::string, core::MaterialTemplateHandle>    materials;

    if (renderer && doc.contains("assets")) {
        const auto &assets = doc["assets"];

        if (assets.contains("shaders")) {
            for (const auto &[name, spec] : assets["shaders"].items()) {
                const std::string vertSrc = loaders::ShaderLoader::load(
                    assetsDir + "/" + spec.at("vert").get<std::string>());
                const std::string fragSrc = loaders::ShaderLoader::load(
                    assetsDir + "/" + spec.at("frag").get<std::string>());
                shaders[name] = renderer->createShader(vertSrc, fragSrc);
            }
        }

        if (assets.contains("materials")) {
            for (const auto &[name, spec] : assets["materials"].items()) {
                const std::string shaderName = spec.at("shader");
                auto shIt = shaders.find(shaderName);
                if (shIt == shaders.end())
                    throw std::runtime_error("SceneLoader: unknown shader '" + shaderName + "'");

                core::UniformValueMap defaults;
                if (spec.contains("defaultValues")) {
                    for (const auto &[uname, uval] : spec["defaultValues"].items())
                        defaults[uname] = parseUniformValue(uval);
                }

                const auto tmplHandle = renderer->createMaterial(MaterialTemplate{
                    .shaderHandle  = shIt->second,
                    .renderState   = {},
                    .defaultValues = std::move(defaults),
                });
                materials[name]        = tmplHandle;
                result.materials[name] = tmplHandle;
            }
        }

        if (assets.contains("meshes")) {
            for (const auto &[name, spec] : assets["meshes"].items())
                meshes[name] = loadMesh(spec, assetsDir, *renderer);
        }

        if (assets.contains("textures")) {
            for (const auto &[name, spec] : assets["textures"].items())
                textures[name] = loadTexture(spec, assetsDir, *renderer);
        }
    }

    // ── Object creation ────────────────────────────────────────────────────────
    // Objects are processed in document order; a parent must appear before its children.
    if (!doc.contains("objects")) return result;

    for (const auto &spec : doc["objects"]) {
        const std::string name = spec.at("name");

        world::GameObject *parent = nullptr;
        if (spec.contains("parent")) {
            auto it = result.objects.find(spec["parent"].get<std::string>());
            if (it != result.objects.end()) parent = it->second;
        }

        auto &obj          = scene.createObject(name, parent);
        result.objects[name] = &obj;

        // Transform
        if (spec.contains("position")) {
            auto p = spec["position"];
            obj.transform.setLocalPosition({p[0], p[1], p[2]});
        }
        if (spec.contains("rotation")) {
            auto r = spec["rotation"];        // stored as [x, y, z, w]
            obj.transform.setLocalRotation(glm::quat{
                r[3].get<float>(), r[0].get<float>(),
                r[1].get<float>(), r[2].get<float>()});
        }
        if (spec.contains("scale")) {
            auto s = spec["scale"];
            obj.transform.setLocalScale({s[0], s[1], s[2]});
        }

        // Camera component
        if (spec.contains("camera")) {
            const auto &cam = spec["camera"];
            obj.camera = world::CameraComponent{
                .fov  = cam.value("fov",  60.0f),
                .near = cam.value("near",  0.1f),
                .far  = cam.value("far", 200.0f),
            };
        }

        // Render component (requires renderer + resolved assets)
        if (renderer && spec.contains("render")) {
            const auto &rc = spec["render"];

            auto meshIt = meshes.find(rc.at("mesh").get<std::string>());
            auto matIt  = materials.find(rc.at("material").get<std::string>());

            if (meshIt != meshes.end() && matIt != materials.end()) {
                MaterialInstance mat{matIt->second};
                if (rc.contains("textures")) {
                    for (const auto &[uniform, texName] : rc["textures"].items()) {
                        auto texIt = textures.find(texName.get<std::string>());
                        if (texIt != textures.end())
                            mat.addTexture(uniform, texIt->second);
                    }
                }
                obj.render = world::RenderComponent{
                    .mesh     = meshIt->second,
                    .material = std::move(mat),
                };
            }
        }
    }

    return result;
}

} // namespace sonnet::scene
