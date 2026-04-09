add_library(sonnet_renderer_opengl STATIC
    src/GlRendererBackend.cpp
    src/GlShader.cpp
    src/GlShaderCompiler.cpp
    src/GlTexture2D.cpp
    src/GlRenderTarget.cpp
)

add_library(sonnet::renderer::opengl ALIAS sonnet_renderer_opengl)

target_include_directories(sonnet_renderer_opengl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Private include for internal GlUtils.h
target_include_directories(sonnet_renderer_opengl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sonnet_renderer_opengl
    PUBLIC
        sonnet::api
        glm::glm
        glad
)
