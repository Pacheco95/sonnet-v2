# Test fixtures

These models are vendored from the official Khronos
[glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
repository at upstream commit
`5109ab2a499c5a2c784b86e460fa491d52256e25`.

Each subdirectory mirrors a single sample's `glTF/` (or `glTF-Binary/`)
folder verbatim. The full per-asset license terms live in each upstream
sample's `LICENSE.md`.

| Fixture                                              | Upstream path                                                       | License        |
| ---------------------------------------------------- | ------------------------------------------------------------------- | -------------- |
| `Triangle/Triangle.gltf` + `.bin`                    | `Models/Triangle/glTF/`                                             | CC0 1.0        |
| `TriangleWithoutIndices/*.gltf` + `.bin`             | `Models/TriangleWithoutIndices/glTF/`                               | CC0 1.0        |
| `BoxTextured/BoxTextured.gltf` + `0.bin` + `.png`    | `Models/BoxTextured/glTF/`                                          | CC-BY 4.0      |
| `BoxTextured/BoxTextured.glb`                        | `Models/BoxTextured/glTF-Binary/`                                   | CC-BY 4.0      |
| `BoxAnimated/BoxAnimated.gltf` + `0.bin`             | `Models/BoxAnimated/glTF/`                                          | CC-BY 4.0      |
| `RiggedSimple/RiggedSimple.gltf` + `0.bin`           | `Models/RiggedSimple/glTF/`                                         | CC-BY 4.0      |

These files are used solely as test fixtures for ModelLoader and are not
shipped in any engine binary or runtime asset bundle.
