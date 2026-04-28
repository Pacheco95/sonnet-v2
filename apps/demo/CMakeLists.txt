# Both backends now build the full demo (shadows/IBL/post/editor). The Vulkan
# build keeps main_vk.cpp around as a minimal sanity-check executable for
# bring-up but the primary `demo` target is the same source set on both sides.
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
)
if(SONNET_USE_OPENGL)
    target_link_libraries(demo PRIVATE glad)
endif()

# Bake the source-tree assets path into the binary so the demo can always find
# its assets regardless of the working directory at runtime.
target_compile_definitions(demo PRIVATE
    DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
