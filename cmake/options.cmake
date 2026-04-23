option(SONNET_BUILD_TESTS "Build Sonnet tests" ON)

set(SONNET_RENDERER_BACKEND "Auto" CACHE STRING
    "Renderer backend: Auto | OpenGL | Vulkan")
set_property(CACHE SONNET_RENDERER_BACKEND PROPERTY STRINGS Auto OpenGL Vulkan)

if(SONNET_RENDERER_BACKEND STREQUAL "Auto")
    if(APPLE)
        set(_sonnet_resolved_backend "Vulkan")
    else()
        set(_sonnet_resolved_backend "OpenGL")
    endif()
else()
    set(_sonnet_resolved_backend "${SONNET_RENDERER_BACKEND}")
endif()

if(_sonnet_resolved_backend STREQUAL "Vulkan")
    set(SONNET_USE_VULKAN TRUE)
    set(SONNET_USE_OPENGL FALSE)
elseif(_sonnet_resolved_backend STREQUAL "OpenGL")
    set(SONNET_USE_VULKAN FALSE)
    set(SONNET_USE_OPENGL TRUE)
else()
    message(FATAL_ERROR "Unknown SONNET_RENDERER_BACKEND: ${SONNET_RENDERER_BACKEND} (expected Auto, OpenGL, or Vulkan)")
endif()

message(STATUS "Renderer backend: ${_sonnet_resolved_backend}")
