#pragma once

#include <sonnet/api/render/CPUMesh.h>

#include <glm/glm.hpp>

namespace sonnet::primitives {

// All primitives use Position + TexCoord + Normal vertex layout.

// Box centered at origin with given size. 24 vertices (4 per face), 36 indices.
// Flat normals per face; UV scaled by face edge lengths.
[[nodiscard]] api::render::CPUMesh makeBox(glm::vec3 size);

// UV sphere of radius 1 centered at origin.
// smooth=true: shared vertices, smooth normals.
// smooth=false: duplicated vertices per triangle, flat normals.
[[nodiscard]] api::render::CPUMesh makeUVSphere(int segmentsX, int segmentsY, bool smooth = true);

// Quad on the XY plane facing +Z, centered at origin, with given size.
// 4 vertices, 6 indices, UVs in [0,1].
[[nodiscard]] api::render::CPUMesh makeQuad(glm::vec2 size);

} // namespace sonnet::primitives
