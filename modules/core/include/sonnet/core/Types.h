#pragma once

#include "Handle.h"
#include "ReadonlyBuffer.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

#include <glm/glm.hpp>

namespace sonnet::core {

// ── Uniform value types ───────────────────────────────────────────────────────

using Sampler = std::uint8_t;

using UniformValue = std::variant<
    int,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    glm::mat4,
    Sampler
>;

enum class UniformType { Int, Float, Vec2, Vec3, Vec4, Mat4, Sampler };

struct UniformDescriptor {
    UniformType type = UniformType::Float;
    int location    = -1;
};

using UniformName          = std::string;
using UniformDescriptorMap = std::unordered_map<UniformName, UniformDescriptor>;
using UniformValueMap      = std::unordered_map<UniformName, UniformValue>;

// ── GPU / CPU resource types ─────────────────────────────────────────────────

using ShaderProgram = std::uint32_t;
using Texels        = ReadonlyBuffer<std::byte>;

// ── Typed resource handles ────────────────────────────────────────────────────

struct ShaderTag           {};
struct MaterialTemplateTag {};
struct CPUTextureBufferTag {};
struct GPUTextureTag       {};
struct CPUMeshTag          {};
struct GPUMeshTag          {};
struct RenderTargetTag     {};

using ShaderHandle           = Handle<ShaderTag>;
using MaterialTemplateHandle = Handle<MaterialTemplateTag>;
using CPUTextureBufferHandle = Handle<CPUTextureBufferTag>;
using GPUTextureHandle       = Handle<GPUTextureTag>;
using CPUMeshHandle          = Handle<CPUMeshTag>;
using GPUMeshHandle          = Handle<GPUMeshTag>;
using RenderTargetHandle     = Handle<RenderTargetTag>;

} // namespace sonnet::core
