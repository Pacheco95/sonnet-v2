add_executable(demo main.cpp)

target_link_libraries(demo PRIVATE
    sonnet::renderer::frontend
    sonnet::primitives
    sonnet::scene
    sonnet::world
    sonnet::ui
    imgui::imgui
    sonnet::window
    sonnet::input
    glm::glm
)

# Bake the source-tree assets path into the binary so the demo can always find
# its assets regardless of the working directory at runtime.
target_compile_definitions(demo PRIVATE
    DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
