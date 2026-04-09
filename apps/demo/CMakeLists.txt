add_executable(demo main.cpp)

target_link_libraries(demo PRIVATE
    sonnet::renderer::frontend
    sonnet::primitives
    sonnet::loaders
    sonnet::window
    sonnet::input
    glm::glm
)

# Copy assets next to the executable so relative paths work at runtime.
add_custom_command(TARGET demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:demo>/assets
)
