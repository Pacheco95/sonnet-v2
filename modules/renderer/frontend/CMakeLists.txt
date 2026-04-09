add_library(sonnet_renderer_frontend STATIC
    src/Renderer.cpp
)

add_library(sonnet::renderer::frontend ALIAS sonnet_renderer_frontend)

target_include_directories(sonnet_renderer_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_renderer_frontend PUBLIC
    sonnet::api
    sonnet::renderer::opengl
    glm::glm
)
