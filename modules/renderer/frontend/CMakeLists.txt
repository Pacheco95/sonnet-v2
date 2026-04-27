add_library(sonnet_renderer_frontend STATIC
    src/Renderer.cpp
    src/BackendFactory.cpp
)

add_library(sonnet::renderer::frontend ALIAS sonnet_renderer_frontend)

target_include_directories(sonnet_renderer_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_renderer_frontend PUBLIC
    sonnet::api
    glm::glm
)

if(SONNET_USE_OPENGL)
    target_link_libraries(sonnet_renderer_frontend PUBLIC sonnet::renderer::opengl)
endif()
if(SONNET_USE_VULKAN)
    target_link_libraries(sonnet_renderer_frontend PUBLIC sonnet::renderer::vulkan)
endif()

add_subdirectory(tests)
