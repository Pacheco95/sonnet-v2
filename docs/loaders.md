# Loaders

`modules/loaders/` exposes three small static-method helpers for the on-disk asset formats the engine consumes.

## TextureLoader

Wraps `stb_image`. Returns an `api::render::CPUTextureBuffer` (width, height, channels, byte buffer):

```cpp
auto cpu = sonnet::loaders::TextureLoader::load("path/tex.png");
```

`TextureLoadOptions::flipVertically` defaults to `true` so `(0, 0)` is bottom-left, matching OpenGL conventions. The Vulkan backend handles UV-direction internally so the same default works for both backends.

## ShaderLoader

A trivial file-to-string helper:

```cpp
const std::string vert = sonnet::loaders::ShaderLoader::load("shaders/lit.vert");
```

The renderer expects GLSL source; under Vulkan this is compiled to SPIR-V at runtime by `VkShaderCompiler` (glslang).

## ModelLoader

Wraps Assimp. Two entry points, both static:

### `ModelLoader::load(path)`

Returns `std::vector<CPUMesh>` — bare geometry with `Position(0)`, `TexCoord(2)`, `Normal(3)`, `Tangent(4)`, `Bitangent(5)` attributes. Useful when the application provides materials separately. Post-processing applied: triangulate, generate normals, calculate tangents.

### `ModelLoader::loadAll(path)`

Returns a `LoadedModel` containing meshes, the full `LoadedNode` hierarchy (mirroring Assimp's node tree, including empty nodes used as animation targets), PBR material data, and animation clips. Intended for glTF/GLB; OBJ is supported by `load()`.

`LoadedMesh` includes:
- `mesh` — the `CPUMesh`.
- `material` — extracted PBR data (`albedo`, `normal`, `orm`, `emissive` textures plus `albedoFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `alphaMask`, `alphaCutoff`).
- `boundsCenter` + `boundsRadius` — local-space bounding sphere computed during load.
- `hasSkin` + `bones` — populated for skinned meshes; bones carry a name and inverse bind matrix.

`MeshTexture` represents a texture dependency: either a path (external) or pre-decoded `CPUTextureBuffer` (embedded GLB binary). Embedded textures are decoded eagerly via stb_image during load.

`AnimationClip` carries a `name`, `duration` (seconds), and `AnimationChannel`s. Each channel targets a node by name and contains keyframe arrays for translation, rotation, and scale.

`computeBoundingSphere(mesh)` is exposed separately — given a CPU mesh whose first attribute is a `vec3` position, returns `{centroid, radius}`.

## Why Assimp?

`cmake/dependencies.cmake` builds Assimp with only the OBJ and glTF importers enabled — FBX and the rest are off. The bundled zlib is also disabled because it conflicts with the macOS SDK; assimp picks up the system zlib instead.
