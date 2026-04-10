#pragma once

#include <sonnet/world/Transform.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace sonnet::world {

// Projection parameters for a camera attached to a GameObject.
// The view matrix is derived from the owning GameObject's Transform;
// this struct only stores what the transform cannot express.
struct CameraComponent {
    float fov  = 60.0f;   // vertical field-of-view, degrees
    float near = 0.1f;
    float far  = 200.0f;

    // Perspective projection for the given aspect ratio (width / height).
    [[nodiscard]] glm::mat4 projectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, near, far);
    }

    // View matrix derived from the transform's world matrix.
    // Assumes the transform has no non-uniform scale on the camera object.
    [[nodiscard]] glm::mat4 viewMatrix(const Transform &t) const {
        return glm::inverse(t.getModelMatrix());
    }
};

} // namespace sonnet::world
