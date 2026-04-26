# Scene file format

`modules/scene/` provides `SceneLoader`, a JSON loader that builds a `world::Scene`, registers meshes/materials/textures with the `Renderer`, registers physics bodies with `PhysicsSystem`, and returns a `LoadedScene` lookup table for the application.

The full JSON Schema for the scene format lives at `scene.schema.json` in the repository root.

## Top-level structure

```json
{
  "assets": {
    "shaders":  { "<name>": { "vert": "shaders/foo.vert", "frag": "shaders/foo.frag" } },
    "materials":{ "<name>": { "shader": "<shaderName>", "defaultValues": { ... } } },
    "meshes":   { "<name>": "path/to/file.obj" | "path/to/file.glb" | { "primitive": "box", "size": [w,h,d] } | { "model": "path.glb", "material": "<materialName>" } },
    "textures": { "<name>": "path/to/tex.png" | { "color": [r, g, b] } }
  },
  "objects": [
    {
      "name":     "ObjName",
      "parent":   "ParentName",
      "enabled":  true,
      "position": [x, y, z],
      "rotation": [x, y, z, w],
      "scale":    [x, y, z],
      "camera":   { "fov": 60, "near": 0.1, "far": 200 },
      "render":   { "mesh": "<meshName>", "material": "<materialName>", "textures": { "uTex": "<textureName>" } },
      "light":    { "type": "directional"|"point", "color": [r,g,b], "intensity": 1.0, "direction": [x,y,z] },
      "physics":  { "bodyType": "static"|"dynamic"|"kinematic", "shapeType": "box"|"sphere", "mass": 1.0, ... },
      "script":   "scripts/rotate.lua"
    }
  ]
}
```

## Asset entries

- **shaders** — pair of GLSL source paths. Compiled by `Renderer::createShader`.
- **materials** — references a shader name and supplies a `defaultValues` map. Values may be `float`, `[x, y]`, `[x, y, z]`, or `[x, y, z, w]`.
- **meshes** — three forms:
  - String → file path passed to `ModelLoader`.
  - `{ "primitive": "box" | "sphere" | "quad", ... }` → built by `primitives::makeBox` / `makeUVSphere` / `makeQuad`.
  - `{ "model": "<path>", "material": "<materialName>" }` → loads model meshes and applies the named material template; PBR textures from the model are bound automatically.
- **textures** — file path, or `{ "color": [r, g, b] }` to allocate a 1×1 solid texture (values 0–255).

## Object entries

Every object is created via `Scene::createObject(name, parent)`. Optional fields populate components:

- `camera` → `CameraComponent`.
- `render.mesh` + `render.material` → `RenderComponent` with a fresh `MaterialInstance` whose textures map is populated from `render.textures`.
- `light` → `LightComponent`.
- `physics` → registers a body with `PhysicsSystem`.
- `script` → relative path that the application reads separately (the demo wires Lua scripts in `main.cpp` via `LuaScriptRuntime::attachScript`).

`enabled = false` skips render-component creation for this object (used to keep scene-tree-visible-but-disabled objects cheap).

## Loading

```cpp
SceneLoader sl;
sl.registerTexture("ssaoTex", ssaoTexHandle);     // optional: register runtime textures
auto loaded = sl.load(sceneFile, assetsDir, scene, renderer, &physics);

// Look-ups in the returned LoadedScene
auto &cam = *loaded.objects.at("Camera");
const auto matHandle = loaded.materials.at("lit");
```

`registerTexture()` is the hook for wiring render-target outputs (e.g. shadow maps, G-buffer attachments) into materials by name before `load()` runs. The returned `LoadedScene` exposes:

- `objects` — name → `GameObject*`.
- `materials` — name → `MaterialTemplateHandle`.
- `directionalLights` / `pointLights` — parsed from `LightComponent` data, ready to drop into a `FrameContext`.

`loadFromString(json, ...)` is provided for tests; passing a null `Renderer*` skips asset creation and render-component assignment.
