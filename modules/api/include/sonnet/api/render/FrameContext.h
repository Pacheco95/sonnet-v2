#pragma once

#include <sonnet/api/render/Light.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace sonnet::api::render {

struct FrameContext {
    const glm::mat4 &viewMatrix;
    const glm::mat4 &projectionMatrix;
    const glm::vec3 &viewPosition;
    std::uint32_t    viewportWidth;
    std::uint32_t    viewportHeight;
    float            deltaTime;

    std::optional<DirectionalLight> directionalLight;
    std::vector<PointLight>         pointLights;
    std::optional<glm::mat4>        lightSpaceMatrix;
};

} // namespace sonnet::api::render
