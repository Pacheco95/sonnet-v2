add_library(sonnet_physics STATIC
    src/PhysicsSystem.cpp
)

add_library(sonnet::physics ALIAS sonnet_physics)

target_include_directories(sonnet_physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_physics
    PUBLIC  sonnet::world
    PRIVATE Jolt
)

sonnet_add_module_test(sonnet_physics
    tests/PhysicsSystemTests.cpp
)
