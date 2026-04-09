add_executable(demo main.cpp)

target_link_libraries(demo PRIVATE
    sonnet::renderer::frontend
    sonnet::window
    sonnet::input
    glm::glm
)
