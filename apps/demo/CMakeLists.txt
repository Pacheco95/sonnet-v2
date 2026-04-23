# Phase 1: the full demo (shadows/IBL/post/editor) still links against raw
# OpenGL. Under Vulkan we build a minimal clear-to-color loop from main_vk.cpp
# until Phase 7 refactors every pass onto IRendererBackend.
if(SONNET_USE_VULKAN)
    add_executable(demo main_vk.cpp)

    target_link_libraries(demo PRIVATE
        sonnet::renderer::frontend
        sonnet::renderer::vulkan
        sonnet::window
        sonnet::input
        sonnet::ui
        imgui::imgui
    )
else()
    add_executable(demo
        main.cpp
        ShaderRegistry.cpp
        ShadowMaps.cpp
        RenderTargets.cpp
        RenderGraph.cpp
        PostProcess.cpp
        EditorUI.cpp
    )

    target_link_libraries(demo PRIVATE
        sonnet::renderer::frontend
        sonnet::primitives
        sonnet::physics
        sonnet::scene
        sonnet::loaders
        sonnet::world
        sonnet::ui
        imgui::imgui
        sonnet::window
        sonnet::input
        sonnet::scripting
        glm::glm
        stb::image
        nlohmann_json::nlohmann_json
        glad
    )
endif()

# Bake the source-tree assets path into the binary so the demo can always find
# its assets regardless of the working directory at runtime.
target_compile_definitions(demo PRIVATE
    DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
