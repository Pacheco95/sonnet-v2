# Mesh primitives

`modules/primitives/` provides three programmatic `CPUMesh` builders. All three use the same `Position(0) + TexCoord(2) + Normal(3)` vertex layout so they can be drawn by the same shaders as loaded models (which add tangents and bitangents).

## API

```cpp
#include <sonnet/primitives/MeshPrimitives.h>

CPUMesh box    = sonnet::primitives::makeBox({width, height, depth});
CPUMesh sphere = sonnet::primitives::makeUVSphere(segmentsX, segmentsY,
                                                   /*smooth=*/true);
CPUMesh quad   = sonnet::primitives::makeQuad({width, height});
```

### `makeBox(size)`

Centred at origin with the given size. **24 vertices** (4 per face, duplicated so each face has its own flat normal), 36 indices. UV coordinates are scaled by face edge lengths so a box with non-uniform size maps a single texture without distortion.

### `makeUVSphere(segmentsX, segmentsY, smooth)`

UV sphere of radius 1, centred at origin.

- `smooth = true` — vertices are shared between adjacent faces; each vertex's normal is its position normalised, producing smooth shading.
- `smooth = false` — vertices are duplicated per triangle; each triangle gets a flat face normal.

`segmentsX` is the number of longitudinal divisions, `segmentsY` the number of latitudinal divisions.

### `makeQuad(size)`

A two-triangle quad on the XY plane facing `+Z`, centred at origin. 4 vertices, 6 indices, UVs span `[0, 1]`. Used by post-process passes for fullscreen draws and as the geometry for textured 2D elements (e.g. the BRDF LUT capture quad).

## Scene integration

The JSON scene format (see [Scene file format](scene-files.md)) accepts these primitives directly:

```json
"meshes": {
  "floor":  { "primitive": "box",    "size": [6.0, 0.1, 6.0] },
  "ball":   { "primitive": "sphere", "segmentsX": 24, "segmentsY": 12 },
  "screen": { "primitive": "quad",   "size": [16, 9] }
}
```
