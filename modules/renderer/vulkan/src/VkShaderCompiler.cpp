#include <sonnet/renderer/vulkan/VkShaderCompiler.h>
#include <sonnet/renderer/vulkan/VkShader.h>

#include "VkDevice.h"
#include "VkUtils.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

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

    struct Cleanup {
        glslang_shader_t *s;
        ~Cleanup() { glslang_shader_delete(s); }
    } cleanup{shader};

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
    struct ProgCleanup {
        glslang_program_t *p;
        ~ProgCleanup() { glslang_program_delete(p); }
    } progCleanup{program};

    glslang_program_add_shader(program, shader);
    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        throw VulkanError(std::string("glslang link (") + stageName + "): " +
                          (glslang_program_get_info_log(program) ?: "") +
                          (glslang_program_get_info_debug_log(program) ?: ""));
    }

    glslang_program_SPIRV_generate(program, stage);
    const std::size_t wordCount = glslang_program_SPIRV_get_size(program);
    std::vector<std::uint32_t> spirv(wordCount);
    glslang_program_SPIRV_get(program, spirv.data());
    return spirv;
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
    return std::make_unique<VkShader>(m_device,
                                       vertexSrc, fragmentSrc,
                                       std::move(vertSpv), std::move(fragSpv));
}

} // namespace sonnet::renderer::vulkan
