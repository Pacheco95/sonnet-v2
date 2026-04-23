add_library(sonnet_renderer_vulkan STATIC
    src/VkRendererBackend.cpp
    src/VkInstance.cpp
    src/VkDevice.cpp
    src/VkSwapchain.cpp
    src/VkCommandContext.cpp
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
        glfw                      # glfwCreateWindowSurface (via GLFWWindow)
        spdlog::spdlog
)

# VMA is a single-header library; define the implementation macro in VkDevice.cpp.
# Silence VMA's own noisy warnings under -Werror.
set_source_files_properties(src/VkDevice.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-nullability-completeness;-Wno-unused-variable;-Wno-unused-parameter;-Wno-missing-field-initializers"
)
