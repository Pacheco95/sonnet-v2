#pragma once

#include <sonnet/world/Transform.h>

#include <glm/glm.hpp>

#include <vector>

namespace sonnet::world {

// Per-object GPU skinning state. Holds the inverse bind matrices (constant,
// set once at load time) and raw pointers to the bone node Transforms (driven
// by the AnimationPlayer each frame).
//
// Each frame the owner's bone palette is:
//   boneMatrix[i] = boneTransforms[i]->getModelMatrix() * inverseBindMatrices[i]
//
// The palette is written to the material's "uBoneMatrices[i]" uniforms before
// the draw call.
struct SkinComponent {
    int                    numBones = 0;
    std::vector<glm::mat4> inverseBindMatrices; // bind-pose inverse, one per bone
    std::vector<Transform*> boneTransforms;     // raw ptrs into Scene; same order as above
};

} // namespace sonnet::world
