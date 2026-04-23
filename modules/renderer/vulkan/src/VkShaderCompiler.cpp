#include <sonnet/renderer/vulkan/VkShaderCompiler.h>
#include <sonnet/renderer/vulkan/VkShader.h>

#include "VkDevice.h"
#include "VkUtils.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <spirv_reflect.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace sonnet::renderer::vulkan {

namespace {

std::vector<std::uint32_t> compileStage(glslang_stage_t stage,
                                        const std::string &source,
                                        const char *stageName) {
    glslang_input_t input{};
    input.language                          = GLSLANG_SOURCE_GLSL;
    input.stage                             = stage;
    input.client                            = GLSLANG_CLIENT_VULKAN;
    input.client_version                    = GLSLANG_TARGET_VULKAN_1_2;
    input.target_language                   = GLSLANG_TARGET_SPV;
    input.target_language_version           = GLSLANG_TARGET_SPV_1_5;
    input.code                              = source.c_str();
    input.default_version                   = 460;
    input.default_profile                   = GLSLANG_CORE_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible                = false;
    input.messages                          = GLSLANG_MSG_DEFAULT_BIT;
    input.resource                          = glslang_default_resource();

    glslang_shader_t *shader = glslang_shader_create(&input);
    if (!shader) {
        throw VulkanError(std::string("glslang_shader_create failed for ") + stageName);
    }

    struct Cleanup { glslang_shader_t *s; ~Cleanup() { glslang_shader_delete(s); } } cleanup{shader};

    if (!glslang_shader_preprocess(shader, &input)) {
        throw VulkanError(std::string("glslang preprocess (") + stageName + "): " +
                          (glslang_shader_get_info_log(shader) ?: "") +
                          (glslang_shader_get_info_debug_log(shader) ?: ""));
    }
    if (!glslang_shader_parse(shader, &input)) {
        throw VulkanError(std::string("glslang parse (") + stageName + "): " +
                          (glslang_shader_get_info_log(shader) ?: "") +
                          (glslang_shader_get_info_debug_log(shader) ?: ""));
    }

    glslang_program_t *program = glslang_program_create();
    struct ProgCleanup { glslang_program_t *p; ~ProgCleanup() { glslang_program_delete(p); } } pc{program};

    glslang_program_add_shader(program, shader);
    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        throw VulkanError(std::string("glslang link (") + stageName + "): " +
                          (glslang_program_get_info_log(program) ?: "") +
                          (glslang_program_get_info_debug_log(program) ?: ""));
    }

    glslang_program_SPIRV_generate(program, stage);
    std::vector<std::uint32_t> spirv(glslang_program_SPIRV_get_size(program));
    glslang_program_SPIRV_get(program, spirv.data());
    return spirv;
}

// Minimal SPIRV-Reflect wrapper that deallocates on scope exit.
class ReflectModule {
public:
    explicit ReflectModule(const std::vector<std::uint32_t> &spirv) {
        const auto res = spvReflectCreateShaderModule(
            spirv.size() * sizeof(std::uint32_t), spirv.data(), &m_module);
        if (res != SPV_REFLECT_RESULT_SUCCESS) {
            throw VulkanError("spvReflectCreateShaderModule failed");
        }
    }
    ~ReflectModule() { spvReflectDestroyShaderModule(&m_module); }
    ReflectModule(const ReflectModule &)            = delete;
    ReflectModule &operator=(const ReflectModule &) = delete;

    [[nodiscard]] SpvReflectShaderModule       *get()       { return &m_module; }
    [[nodiscard]] const SpvReflectShaderModule *get() const { return &m_module; }

private:
    SpvReflectShaderModule m_module{};
};

VkFormat spvFormatToVk(SpvReflectFormat fmt) {
    switch (fmt) {
        case SPV_REFLECT_FORMAT_R32_SFLOAT:           return VK_FORMAT_R32_SFLOAT;
        case SPV_REFLECT_FORMAT_R32G32_SFLOAT:        return VK_FORMAT_R32G32_SFLOAT;
        case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:     return VK_FORMAT_R32G32B32_SFLOAT;
        case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case SPV_REFLECT_FORMAT_R32_SINT:             return VK_FORMAT_R32_SINT;
        case SPV_REFLECT_FORMAT_R32G32_SINT:          return VK_FORMAT_R32G32_SINT;
        case SPV_REFLECT_FORMAT_R32G32B32_SINT:       return VK_FORMAT_R32G32B32_SINT;
        case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:    return VK_FORMAT_R32G32B32A32_SINT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkDescriptorType spvDescriptorTypeToVk(SpvReflectDescriptorType t) {
    switch (t) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:                return VK_DESCRIPTOR_TYPE_SAMPLER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:          return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:          return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:       return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        default: return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

// Merge reflected bindings from one stage into the accumulator, promoting
// existing stage flags when the same (set, binding) appears in both stages.
void mergeStage(std::vector<std::vector<VkDescriptorSetLayoutBinding>> &accumSets,
                const SpvReflectShaderModule *mod,
                VkShaderStageFlagBits stage) {
    std::uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(mod, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding *> bindings(count);
    spvReflectEnumerateDescriptorBindings(mod, &count, bindings.data());

    for (const auto *b : bindings) {
        if (b->set >= accumSets.size()) accumSets.resize(b->set + 1);
        auto &setBindings = accumSets[b->set];

        const auto it = std::find_if(setBindings.begin(), setBindings.end(),
            [&](const VkDescriptorSetLayoutBinding &x) { return x.binding == b->binding; });

        if (it != setBindings.end()) {
            it->stageFlags |= stage;
        } else {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding         = b->binding;
            lb.descriptorType  = spvDescriptorTypeToVk(b->descriptor_type);
            lb.descriptorCount = b->count;
            lb.stageFlags      = stage;
            setBindings.push_back(lb);
        }
    }
}

void mergePushConstants(std::vector<VkPushConstantRange> &accum,
                        const SpvReflectShaderModule *mod,
                        VkShaderStageFlagBits stage) {
    std::uint32_t count = 0;
    spvReflectEnumeratePushConstantBlocks(mod, &count, nullptr);
    std::vector<SpvReflectBlockVariable *> blocks(count);
    spvReflectEnumeratePushConstantBlocks(mod, &count, blocks.data());

    for (const auto *b : blocks) {
        // Vulkan spec: one push-constant range per stage is the simplest pattern.
        VkPushConstantRange r{};
        r.stageFlags = stage;
        r.offset     = b->offset;
        r.size       = b->size;

        // If another stage already declared a range at the same offset+size,
        // union the stage flags instead of creating a duplicate range. Lots
        // of shaders declare the same push block in both stages (uModel).
        auto it = std::find_if(accum.begin(), accum.end(),
            [&](const VkPushConstantRange &x) {
                return x.offset == r.offset && x.size == r.size;
            });
        if (it != accum.end()) {
            it->stageFlags |= stage;
        } else {
            accum.push_back(r);
        }
    }
}

ShaderVertexAttribute extractInput(const SpvReflectInterfaceVariable &v) {
    ShaderVertexAttribute out{};
    out.location = v.location;
    out.format   = spvFormatToVk(v.format);
    out.name     = v.name ? v.name : "";
    return out;
}

std::vector<ShaderVertexAttribute> extractVertexInputs(const SpvReflectShaderModule *mod) {
    std::uint32_t count = 0;
    spvReflectEnumerateInputVariables(mod, &count, nullptr);
    std::vector<SpvReflectInterfaceVariable *> vars(count);
    spvReflectEnumerateInputVariables(mod, &count, vars.data());

    std::vector<ShaderVertexAttribute> out;
    out.reserve(count);
    for (const auto *v : vars) {
        // Skip built-ins (gl_VertexIndex etc.) — their location is -1 (UINT32_MAX).
        if (v->location == static_cast<std::uint32_t>(-1)) continue;
        out.push_back(extractInput(*v));
    }
    std::sort(out.begin(), out.end(),
        [](const ShaderVertexAttribute &a, const ShaderVertexAttribute &b) {
            return a.location < b.location;
        });
    return out;
}

} // namespace

VkShaderCompiler::VkShaderCompiler(Device &device) : m_device(device) {
    if (glslang_initialize_process() == 0) {
        throw VulkanError("glslang_initialize_process failed");
    }
}

VkShaderCompiler::~VkShaderCompiler() {
    glslang_finalize_process();
}

std::unique_ptr<api::render::IShader> VkShaderCompiler::operator()(
    const std::string &vertexSrc, const std::string &fragmentSrc) const {
    auto vertSpv = compileStage(GLSLANG_STAGE_VERTEX,   vertexSrc,   "vertex");
    auto fragSpv = compileStage(GLSLANG_STAGE_FRAGMENT, fragmentSrc, "fragment");

    ReflectModule vertMod(vertSpv);
    ReflectModule fragMod(fragSpv);

    ShaderReflection reflection;
    mergeStage(reflection.setBindings, vertMod.get(), VK_SHADER_STAGE_VERTEX_BIT);
    mergeStage(reflection.setBindings, fragMod.get(), VK_SHADER_STAGE_FRAGMENT_BIT);
    mergePushConstants(reflection.pushConstantRanges, vertMod.get(), VK_SHADER_STAGE_VERTEX_BIT);
    mergePushConstants(reflection.pushConstantRanges, fragMod.get(), VK_SHADER_STAGE_FRAGMENT_BIT);
    reflection.vertexAttributes = extractVertexInputs(vertMod.get());
    // UniformDescriptorMap population deferred to Phase 3c/3d.

    return std::make_unique<VkShader>(m_device,
                                       vertexSrc, fragmentSrc,
                                       std::move(vertSpv), std::move(fragSpv),
                                       std::move(reflection));
}

} // namespace sonnet::renderer::vulkan
