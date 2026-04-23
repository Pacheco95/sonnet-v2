add_library(sonnet_renderer_vulkan STATIC
    src/VkRendererBackend.cpp
    src/VkInstance.cpp
    src/VkDevice.cpp
    src/VkSwapchain.cpp
    src/VkCommandContext.cpp
    src/VkGpuBuffer.cpp
    src/VkTexture2D.cpp
    src/VkTextureFactory.cpp
    src/VkSamplerCache.cpp
    src/VkShader.cpp
    src/VkShaderCompiler.cpp
    src/VkVertexInputState.cpp
    src/VkRenderTarget.cpp
    src/VkRenderTargetFactory.cpp
    src/VkFormatMap.cpp
    src/VkUtils.cpp
)

add_library(sonnet::renderer::vulkan ALIAS sonnet_renderer_vulkan)

target_include_directories(sonnet_renderer_vulkan PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(sonnet_renderer_vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sonnet_renderer_vulkan
    PUBLIC
        sonnet::api
        sonnet::window            # GLFWWindow::createVulkanSurface
        glm::glm
    PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        spirv-reflect-static      # SPIR-V reflection for descriptor layouts
        glfw                      # glfwCreateWindowSurface (via GLFWWindow)
        spdlog::spdlog
)

# glslang + SPIRV from the Vulkan SDK. Prefer the modern CMake-module targets
# (Vulkan::glslang, Vulkan::SPIRV) exposed by find_package(Vulkan COMPONENTS);
# fall back to linking by library file where the component import isn't exposed.
if(TARGET Vulkan::glslang)
    target_link_libraries(sonnet_renderer_vulkan PRIVATE Vulkan::glslang)
endif()
if(TARGET Vulkan::SPIRV)
    target_link_libraries(sonnet_renderer_vulkan PRIVATE Vulkan::SPIRV)
endif()
# The default-resource-limits lib exposes glslang_default_resource() for the
# C interface. The Vulkan CMake module doesn't import it as a target, so link
# it by file path from the SDK.
find_library(GLSLANG_DEFAULT_RESOURCE_LIMITS_LIB
    NAMES glslang-default-resource-limits
    HINTS ${Vulkan_INCLUDE_DIR}/../lib
)
if(GLSLANG_DEFAULT_RESOURCE_LIMITS_LIB)
    target_link_libraries(sonnet_renderer_vulkan PRIVATE ${GLSLANG_DEFAULT_RESOURCE_LIMITS_LIB})
endif()

# VMA is a single-header library; define the implementation macro in VkDevice.cpp.
# Silence VMA's own noisy warnings under -Werror.
set_source_files_properties(src/VkDevice.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-nullability-completeness;-Wno-unused-variable;-Wno-unused-parameter;-Wno-missing-field-initializers"
)
