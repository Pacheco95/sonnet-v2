#include <sonnet/scene/SceneLoader.h>

#include <sonnet/api/render/Light.h>
#include <sonnet/api/render/Material.h>
#include <sonnet/loaders/ModelLoader.h>
#include <sonnet/world/AnimationPlayer.h>
#include <sonnet/world/SkinComponent.h>
#include <sonnet/loaders/ShaderLoader.h>
#include <sonnet/loaders/TextureLoader.h>
#include <sonnet/primitives/MeshPrimitives.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/world/CameraComponent.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Scene.h>

#include <nlohmann/json.hpp>

#include <functional>
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

    // Model entries: "meshName": { "model": "path.glb", "material": "lit" }
    // Each model holds multiple sub-meshes, PBR materials, and animation clips.
    struct ModelEntry {
        loaders::LoadedModel loadedModel;
        std::string          materialName; // scene material template to use
    };
    std::unordered_map<std::string, ModelEntry> modelEntries;

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
            for (const auto &[name, spec] : assets["meshes"].items()) {
                if (spec.is_object() && spec.contains("model")) {
                    // glTF/GLB model — load all sub-meshes + materials.
                    const std::string modelPath  = assetsDir + "/" + spec.at("model").get<std::string>();
                    const std::string matName    = spec.value("material", "lit");
                    modelEntries[name] = ModelEntry{
                        loaders::ModelLoader::loadAll(modelPath),
                        matName,
                    };

                } else {
                    meshes[name] = loadMesh(spec, assetsDir, *renderer);
                }
            }
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

        // Light component
        if (spec.contains("light")) {
            const auto &lc   = spec["light"];
            const std::string type = lc.at("type").get<std::string>();
            const auto col   = lc.value("color", json::array({1.0, 1.0, 1.0}));
            const glm::vec3 color{col[0].get<float>(), col[1].get<float>(), col[2].get<float>()};
            const float intensity = lc.value("intensity", 1.0f);

            if (type == "directional") {
                const auto dir = lc.value("direction", json::array({0.0, -1.0, 0.0}));
                result.directionalLights.push_back(api::render::DirectionalLight{
                    .direction = glm::normalize(glm::vec3{
                        dir[0].get<float>(), dir[1].get<float>(), dir[2].get<float>()}),
                    .color     = color,
                    .intensity = intensity,
                });
            } else if (type == "point") {
                result.pointLights.push_back(api::render::PointLight{
                    .position  = obj.transform.getWorldPosition(),
                    .color     = color,
                    .intensity = intensity,
                });
            }
        }

        // Render component (requires renderer + resolved assets)
        if (renderer && spec.contains("render")) {
            const auto &rc = spec["render"];
            const std::string meshName = rc.at("mesh").get<std::string>();

            // ── Model (multi-mesh glTF) ────────────────────────────────────────
            if (auto modelIt = modelEntries.find(meshName); modelIt != modelEntries.end()) {
                const auto &entry = modelIt->second;

                // Helper: resolve the right material template for a mesh.
                // Skinned meshes prefer "skinned_<base>" if available.
                auto resolveMat = [&](bool hasSkin) -> core::MaterialTemplateHandle {
                    if (hasSkin) {
                        const std::string skinnedName = "skinned_" + entry.materialName;
                        if (auto it = materials.find(skinnedName); it != materials.end())
                            return it->second;
                    }
                    auto it = materials.find(entry.materialName);
                    if (it == materials.end())
                        throw std::runtime_error("SceneLoader: model material '" +
                                                 entry.materialName + "' not found");
                    return it->second;
                };

                // Helper: upload a MeshTexture to the GPU.
                auto uploadTex = [&](const loaders::MeshTexture &tex) -> core::GPUTextureHandle {
                    const ColorSpace cs = tex.srgb ? ColorSpace::sRGB : ColorSpace::Linear;
                    if (tex.embedded && tex.cpuData) {
                        const auto &cd = *tex.cpuData;
                        const auto fmt = cd.channels == 4 ? TextureFormat::RGBA8 : TextureFormat::RGB8;
                        return renderer->createTexture(
                            TextureDesc{.size = {cd.width, cd.height},
                                        .format = fmt, .colorSpace = cs},
                            {}, cd);
                    }
                    auto cpuTex = loaders::TextureLoader::load(tex.path);
                    const auto fmt = cpuTex.channels == 4 ? TextureFormat::RGBA8 : TextureFormat::RGB8;
                    return renderer->createTexture(
                        TextureDesc{.size = {cpuTex.width, cpuTex.height},
                                    .format = fmt, .colorSpace = cs},
                        {}, cpuTex);
                };

                // Pending skin wiring: mesh node → bone names to look up after
                // all GameObjects have been created.
                std::vector<std::pair<world::GameObject*, std::vector<std::string>>> pendingSkins;

                // Recursively create one GameObject per node in the loaded hierarchy.
                // Empty (mesh-less) nodes are created so animation channels that target
                // parent/controller nodes propagate to their mesh children via the
                // Transform hierarchy.
                std::function<void(int, world::GameObject *)> createNodes =
                    [&](int nodeIdx, world::GameObject *parent) {
                        const auto &node = entry.loadedModel.nodes[nodeIdx];
                        const std::string childName = name + "/" + node.name;

                        auto &child = scene.createObject(childName, parent);
                        result.objects[childName] = &child;

                        // Apply the local transform baked into the glTF node.
                        child.transform.setLocalPosition(node.localPosition);
                        child.transform.setLocalRotation(node.localRotation);
                        child.transform.setLocalScale(node.localScale);

                        // Attach mesh (if any).  When a node owns multiple meshes,
                        // only the first is attached directly; extras become sub-children.
                        if (!node.meshIndices.empty()) {
                            const auto &lm = entry.loadedModel.meshes[node.meshIndices[0]];
                            auto gpuMesh = renderer->createMesh(lm.mesh);
                            MaterialInstance childMat{resolveMat(lm.hasSkin)};

                            if (lm.material.albedo.valid())
                                childMat.addTexture("uAlbedo",    uploadTex(lm.material.albedo));
                            if (lm.material.normal.valid())
                                childMat.addTexture("uNormalMap", uploadTex(lm.material.normal));
                            else if (auto it = textures.find("flatNormal"); it != textures.end())
                                childMat.addTexture("uNormalMap", it->second);
                            if (lm.material.orm.valid())
                                childMat.addTexture("uORM",       uploadTex(lm.material.orm));
                            if (lm.material.emissive.valid()) {
                                childMat.addTexture("uEmissive",    uploadTex(lm.material.emissive));
                                childMat.set("uEmissiveFactor", lm.material.emissiveFactor);
                            }
                            if (lm.material.alphaMask)
                                childMat.set("uAlphaCutoff", lm.material.alphaCutoff);
                            childMat.set("uMetallic",     lm.material.metallicFactor);
                            childMat.set("uRoughness",    lm.material.roughnessFactor);
                            childMat.set("uAlbedoFactor", lm.material.albedoFactor);

                            if (rc.contains("textures")) {
                                for (const auto &[uniform, texName] : rc["textures"].items()) {
                                    auto texIt = textures.find(texName.get<std::string>());
                                    if (texIt != textures.end())
                                        childMat.addTexture(uniform, texIt->second);
                                }
                            }
                            child.render = world::RenderComponent{
                                .mesh     = gpuMesh,
                                .material = std::move(childMat),
                            };

                            // For skinned meshes, prepare a SkinComponent.
                            // Bone transform pointers are wired up after all nodes exist.
                            if (lm.hasSkin) {
                                world::SkinComponent skinComp;
                                skinComp.numBones = static_cast<int>(lm.bones.size());
                                skinComp.inverseBindMatrices.reserve(lm.bones.size());
                                skinComp.boneTransforms.resize(lm.bones.size(), nullptr);
                                std::vector<std::string> boneNodeNames;
                                boneNodeNames.reserve(lm.bones.size());
                                for (const auto &b : lm.bones) {
                                    skinComp.inverseBindMatrices.push_back(b.inverseBindMatrix);
                                    boneNodeNames.push_back(name + "/" + b.name);
                                }
                                child.skin = std::move(skinComp);
                                pendingSkins.emplace_back(&child, std::move(boneNodeNames));
                            }
                        }

                        for (int ci : node.children)
                            createNodes(ci, &child);
                    };
                createNodes(entry.loadedModel.rootNode, &obj);

                // Wire up bone Transform pointers now that all nodes exist.
                for (auto &[meshObj, boneNames] : pendingSkins) {
                    auto &skinComp = *meshObj->skin;
                    for (int bi = 0; bi < skinComp.numBones; ++bi) {
                        if (auto it = result.objects.find(boneNames[bi]);
                            it != result.objects.end())
                            skinComp.boneTransforms[bi] = &it->second->transform;
                    }
                }

                // ── Animation player ───────────────────────────────────────────
                // All nodes now have corresponding GameObjects; map every child by
                // its short node name so animation channels can drive them directly.
                if (!entry.loadedModel.animations.empty()) {
                    world::AnimationPlayer player;
                    player.clips = entry.loadedModel.animations;
                    player.addTarget(name, &obj.transform);
                    const std::string prefix = name + "/";
                    for (auto &[childName, childObj] : result.objects) {
                        if (childName.size() > prefix.size() &&
                            childName.substr(0, prefix.size()) == prefix) {
                            player.addTarget(childName.substr(prefix.size()),
                                             &childObj->transform);
                        }
                    }
                    obj.animationPlayer = std::move(player);
                }

            // ── Single mesh ────────────────────────────────────────────────────
            } else {
                auto meshIt = meshes.find(meshName);
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
    }

    return result;
}

} // namespace sonnet::scene
