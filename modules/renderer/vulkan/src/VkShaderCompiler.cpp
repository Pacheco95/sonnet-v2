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

// Preamble injected after #version in every shader compiled under Vulkan.
// Defines VULKAN so shaders can conditionally declare push-constant /
// set=2 PerDraw UBO blocks, and SET(n,b) so `layout(SET(0,0)) ...`
// desugars to `layout(set = 0, binding = 0) ...` for Vulkan and to the
// binding-only form for OpenGL (preamble set in GlShaderCompiler).
constexpr const char *kVulkanPreamble =
    "#define VULKAN 1\n"
    "#define SET(n,b) set = n, binding = b\n";

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

    // Inject VULKAN + SET() macros after #version so shaders can write
    // Vulkan-only constructs (push_constant, set=N) alongside the OpenGL path.
    glslang_shader_set_preamble(shader, kVulkanPreamble);

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

// Map a SPIRV-Reflect block-variable numeric traits to an engine UniformType.
// Best-effort; returns Float for unknown shapes (caller can still write bytes).
core::UniformType uniformTypeFromBlockVariable(const SpvReflectBlockVariable &m) {
    const auto *td = m.type_description;
    if (!td) return core::UniformType::Float;

    const auto &tr = m.numeric;

    // Matrix: 4x4 of float → Mat4.
    if (tr.matrix.column_count == 4 && tr.matrix.row_count == 4 &&
        tr.scalar.width == 32 && (tr.scalar.signedness == 0)) {
        return core::UniformType::Mat4;
    }

    // Vectors.
    if (tr.matrix.column_count == 0 && tr.vector.component_count > 1) {
        switch (tr.vector.component_count) {
            case 2: return core::UniformType::Vec2;
            case 3: return core::UniformType::Vec3;
            case 4: return core::UniformType::Vec4;
            default: break;
        }
    }

    // Scalars.
    if (tr.matrix.column_count == 0 && tr.vector.component_count <= 1) {
        // SpvReflectNumericTraits::Scalar.signedness is 1 for int, 0 for unsigned/float.
        // Float has scalar.width == 32 && signedness == 0 and no OpTypeInt parent; but
        // SPIRV-Reflect doesn't expose that directly in the numeric traits, so use
        // type_description flags instead.
        if (td->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) return core::UniformType::Float;
        if (td->type_flags & SPV_REFLECT_TYPE_FLAG_INT)   return core::UniformType::Int;
    }

    return core::UniformType::Float;
}

// Register a single push-constant member: add/update entry in the parallel
// tables. If the same uniform name already exists (because the other stage
// also declared it) we union the stage flags onto the existing entry.
void registerPushMember(core::UniformDescriptorMap &names,
                        std::vector<ShaderUniformEntry> &entries,
                        const std::string &name,
                        const SpvReflectBlockVariable &member,
                        VkShaderStageFlagBits stage) {
    if (auto it = names.find(name); it != names.end()) {
        auto &existing = entries[static_cast<std::size_t>(it->second.location)];
        if (existing.kind == ShaderUniformKind::PushConstant) {
            existing.stageFlags |= stage;
            return;
        }
    }

    ShaderUniformEntry e{};
    e.kind       = ShaderUniformKind::PushConstant;
    e.offset     = member.offset;
    e.size       = member.size;
    e.stageFlags = stage;

    const int loc = static_cast<int>(entries.size());
    entries.push_back(e);

    core::UniformDescriptor d{};
    d.type     = uniformTypeFromBlockVariable(member);
    d.location = loc;
    names[name] = d;
}

void registerSampler(core::UniformDescriptorMap &names,
                     std::vector<ShaderUniformEntry> &entries,
                     const std::string &name,
                     std::uint32_t set,
                     std::uint32_t binding) {
    if (names.find(name) != names.end()) return; // already registered (other stage)

    ShaderUniformEntry e{};
    e.kind    = ShaderUniformKind::MaterialSampler;
    e.set     = set;
    e.binding = binding;

    const int loc = static_cast<int>(entries.size());
    entries.push_back(e);

    core::UniformDescriptor d{};
    d.type     = core::UniformType::Sampler;
    d.location = loc;
    names[name] = d;
}

void reflectPushConstants(ShaderReflection &out,
                          const SpvReflectShaderModule *mod,
                          VkShaderStageFlagBits stage) {
    std::uint32_t count = 0;
    spvReflectEnumeratePushConstantBlocks(mod, &count, nullptr);
    std::vector<SpvReflectBlockVariable *> blocks(count);
    spvReflectEnumeratePushConstantBlocks(mod, &count, blocks.data());

    for (const auto *b : blocks) {
        // Walk members (flat — nested structs inside push constants are rare).
        for (std::uint32_t i = 0; i < b->member_count; ++i) {
            const auto &m = b->members[i];
            const std::string name = m.name ? m.name : "";
            if (name.empty()) continue;
            registerPushMember(out.uniforms, out.entries, name, m, stage);
        }
    }
}

void reflectMaterialSamplers(ShaderReflection &out,
                             const SpvReflectShaderModule *mod) {
    std::uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(mod, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding *> bindings(count);
    spvReflectEnumerateDescriptorBindings(mod, &count, bindings.data());

    for (const auto *b : bindings) {
        if (b->set != 1) continue;
        if (b->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            b->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE) continue;
        const std::string name = b->name ? b->name : "";
        if (name.empty()) continue;
        registerSampler(out.uniforms, out.entries, name, b->set, b->binding);
    }
}

// Register a PerDraw UBO member (set=2, binding=0): one entry per struct
// field, encoding its byte offset so setUniform can memcpy into the ring.
void registerPerDrawMember(core::UniformDescriptorMap &names,
                           std::vector<ShaderUniformEntry> &entries,
                           const std::string &name,
                           const SpvReflectBlockVariable &member) {
    if (names.find(name) != names.end()) return; // already registered in other stage.

    ShaderUniformEntry e{};
    e.kind   = ShaderUniformKind::PerDrawUbo;
    e.offset = member.offset;
    e.size   = member.size;

    const int loc = static_cast<int>(entries.size());
    entries.push_back(e);

    core::UniformDescriptor d{};
    d.type     = uniformTypeFromBlockVariable(member);
    d.location = loc;
    names[name] = d;
}

void reflectPerDrawUbo(ShaderReflection &out,
                       const SpvReflectShaderModule *mod) {
    std::uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(mod, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding *> bindings(count);
    spvReflectEnumerateDescriptorBindings(mod, &count, bindings.data());

    for (const auto *b : bindings) {
        if (b->set != 2 || b->binding != 0) continue;
        if (b->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;

        // Register each member of the block as a PerDrawUbo uniform.
        for (std::uint32_t i = 0; i < b->block.member_count; ++i) {
            const auto &m = b->block.members[i];
            const std::string name = m.name ? m.name : "";
            if (name.empty()) continue;
            registerPerDrawMember(out.uniforms, out.entries, name, m);
        }
        // Track total size — use the block's reported size (includes trailing
        // padding). Take the max across stages if both declare it.
        if (b->block.size > out.perDrawUboSize) {
            out.perDrawUboSize = b->block.size;
        }
    }
}

} // namespace

VkShaderCompiler::VkShaderCompiler(Device &device, BindState &bindState)
    : m_device(device), m_bindState(bindState) {
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

    // Populate UniformDescriptorMap + parallel entries table so setUniform()
    // can route values correctly. Push constants first (per member), then
    // set=1 samplers, then set=2 PerDraw UBO members.
    reflectPushConstants(reflection, vertMod.get(), VK_SHADER_STAGE_VERTEX_BIT);
    reflectPushConstants(reflection, fragMod.get(), VK_SHADER_STAGE_FRAGMENT_BIT);
    reflectMaterialSamplers(reflection, vertMod.get());
    reflectMaterialSamplers(reflection, fragMod.get());
    reflectPerDrawUbo(reflection, vertMod.get());
    reflectPerDrawUbo(reflection, fragMod.get());

    return std::make_unique<VkShader>(m_device, m_bindState,
                                       vertexSrc, fragmentSrc,
                                       std::move(vertSpv), std::move(fragSpv),
                                       std::move(reflection));
}

} // namespace sonnet::renderer::vulkan
