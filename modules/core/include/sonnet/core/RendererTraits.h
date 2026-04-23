#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace sonnet::core {

enum class Handedness { Left, Right };
enum class UpAxis     { Y_Up, Z_Up };

struct RendererTraits {
    std::string apiName;
    Handedness  handedness;
    UpAxis      up;
    float       ndcZMin; // -1 for OpenGL, 0 for Vulkan/DX
    float       ndcZMax; //  1 for all
    bool        clipSpaceYInverted; // true for Vulkan

    [[nodiscard]] constexpr glm::vec3 forwardVector() const noexcept {
        if (up == UpAxis::Y_Up) {
            return (handedness == Handedness::Right)
                ? glm::vec3(0.0f, 0.0f, -1.0f)
                : glm::vec3(0.0f, 0.0f,  1.0f);
        }
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }

    [[nodiscard]] constexpr glm::vec3 upVector() const noexcept {
        return (up == UpAxis::Y_Up)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    [[nodiscard]] constexpr glm::vec3 rightVector() const noexcept {
        const glm::vec3 fwd = forwardVector();
        const glm::vec3 upV = upVector();
        return (handedness == Handedness::Right)
            ? glm::normalize(glm::cross(fwd, upV))
            : glm::normalize(glm::cross(upV, fwd));
    }
};

namespace presets {
inline constexpr RendererTraits OpenGL  {"OpenGL",  Handedness::Right, UpAxis::Y_Up, -1.0f, 1.0f, false};
inline constexpr RendererTraits Vulkan  {"Vulkan",  Handedness::Right, UpAxis::Y_Up,  0.0f, 1.0f, true};
inline constexpr RendererTraits DirectX {"DirectX", Handedness::Left,  UpAxis::Y_Up,  0.0f, 1.0f, false};

// Compile-time-selected traits matching the active backend. Code that needs
// to build NDC-sensitive matrices (projections, clip-space Y flips) reads
// this rather than plumbing RendererTraits through every API.
inline constexpr const RendererTraits &Active() {
#if defined(SONNET_USE_VULKAN)
    return Vulkan;
#else
    return OpenGL;
#endif
}
} // namespace presets

// Backend-aware projection matrix builders. Every place that was calling
// glm::perspective / glm::ortho should switch to these so clip-space Y flip
// and NDC Z range track the active renderer traits automatically.
namespace projection {

[[nodiscard]] inline glm::mat4 perspective(float fovY, float aspect, float near, float far) {
    const auto &t = presets::Active();
    glm::mat4 p = (t.ndcZMin == 0.0f)
        ? glm::perspectiveRH_ZO(fovY, aspect, near, far)
        : glm::perspectiveRH_NO(fovY, aspect, near, far);
    if (t.clipSpaceYInverted) p[1][1] *= -1.0f;
    return p;
}

[[nodiscard]] inline glm::mat4 ortho(float left, float right, float bottom, float top,
                                     float near, float far) {
    const auto &t = presets::Active();
    glm::mat4 p = (t.ndcZMin == 0.0f)
        ? glm::orthoRH_ZO(left, right, bottom, top, near, far)
        : glm::orthoRH_NO(left, right, bottom, top, near, far);
    if (t.clipSpaceYInverted) p[1][1] *= -1.0f;
    return p;
}

} // namespace projection

} // namespace sonnet::core
