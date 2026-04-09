#pragma once

#include <glm/glm.hpp>
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
} // namespace presets

} // namespace sonnet::core
