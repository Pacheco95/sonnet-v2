# World & scene graph

`modules/world/` provides the runtime scene representation: a flat list of `GameObject`s, a parent/child `Transform` graph, and optional component data on each object.

## Scene

`Scene` (in `Scene.h`) owns a `std::vector<std::unique_ptr<GameObject>>`. Operations:

- `createObject(name, parent = nullptr)` — append a new object. If `parent` is non-null, the new object's `Transform` is attached as a child (`keepWorldTransform = false`).
- `duplicateObject(src)` — shallow clone: copies `Transform`, `RenderComponent`, `LightComponent`, `CameraComponent`. Skips `AnimationPlayer` and `SkinComponent` (those carry per-instance state). Names the clone `"<src.name> (Copy)"` and parents it to the same parent as `src`.
- `destroyObject(obj)` — removes `obj` and every descendant. The caller must clear any raw pointers (selection state, script bindings) before calling.
- `buildRenderQueue(queue, frustumPlanes = nullptr)` — appends one `RenderItem` per enabled object that has a `RenderComponent`. When `frustumPlanes` is provided, objects whose world-space bounding sphere lies entirely outside any plane are culled.

## GameObject

A plain aggregate. The components are `std::optional<...>` so absence is the natural state:

```cpp
struct GameObject {
    std::string                       name;
    bool                              enabled{true};
    Transform                         transform;
    std::optional<RenderComponent>    render;
    std::optional<LightComponent>     light;
    std::optional<CameraComponent>    camera;
    std::optional<AnimationPlayer>    animationPlayer;
    std::optional<SkinComponent>      skin;
};
```

### RenderComponent

Holds a `GPUMeshHandle`, a `MaterialInstance`, and a local-space bounding sphere (`boundsCenter`, `boundsRadius`) used for frustum culling. Default `boundsRadius = FLT_MAX` means "never cull".

### LightComponent

`type` is `Directional` or `Point`; carries colour, intensity, an `enabled` flag, and a `direction` (directional only).

### CameraComponent

Stores `fov` (vertical, degrees), `near`, `far`. View matrix is derived from the owning object's `Transform`; projection uses `core::projection::perspective` so it picks the right NDC range and clip-space Y flip for the active backend.

## Transform

`Transform` is a TRS-style local pose with parent/child links. Local position, local rotation (quaternion), and local scale; world counterparts are computed by walking the parent chain.

Key behaviour:

- `setParent(parent, keepWorldTransform = true)` — re-parents while optionally preserving world position/rotation.
- `getModelMatrix()` is cached and recomputed lazily when `markDirty()` flags it, walking the parent chain.
- `forward()`, `up()`, `right()` return world-space direction vectors derived from the renderer traits' axis convention (Y-up by default).
- `lookAt(target, worldUp)` orients the transform so its forward axis points at `target`.

The constructor takes a `RendererTraits` reference so direction conventions match the active backend; default is `presets::OpenGL` (right-handed, Y-up).

## Animation & skinning

`AnimationPlayer` (in `AnimationPlayer.h`) drives a set of named `Transform*` from `loaders::AnimationClip` keyframe data:

```cpp
AnimationPlayer player;
player.clips = std::move(loadedClips);
player.addTarget("Bone.001", &someTransform);
player.update(dt);   // advances time, writes interpolated TRS
```

It supports linear interpolation for translation and scale, slerp for rotation, with optional looping.

`SkinComponent` carries the per-mesh skinning data: inverse bind matrices and bone-node `Transform*` pointers. The owning application is responsible for computing the bone palette (`boneTransform.getModelMatrix() * inverseBindMatrix`) each frame and writing it into the material's `uBoneMatrices[i]` uniforms before the draw call. The demo does this in `main.cpp` after running the animation players.

## See also

- [Scene file format](scene-files.md) — JSON schema consumed by `SceneLoader`.
- [Loaders](loaders.md) — how meshes, materials, and animation clips arrive on disk.
- [Scripting](scripting.md) — Lua API exposed for `Transform` and other engine types.
