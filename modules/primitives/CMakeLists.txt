add_library(sonnet_primitives STATIC
    src/MeshPrimitives.cpp
)

add_library(sonnet::primitives ALIAS sonnet_primitives)

target_include_directories(sonnet_primitives PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_primitives PUBLIC
    sonnet::api
    glm::glm
)

sonnet_add_module_test(sonnet_primitives
    tests/MeshPrimitivesTests.cpp
)
