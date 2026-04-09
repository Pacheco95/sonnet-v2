#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace sonnet::api::render {

struct FrameContext {
    const glm::mat4 &viewMatrix;
    const glm::mat4 &projectionMatrix;
    const glm::vec3 &viewPosition;
    std::uint32_t    viewportWidth;
    std::uint32_t    viewportHeight;
    float            deltaTime;
};

} // namespace sonnet::api::render
