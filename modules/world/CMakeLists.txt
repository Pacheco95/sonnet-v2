add_library(sonnet_world STATIC
    src/Transform.cpp
    src/Scene.cpp
    src/AnimationPlayer.cpp
)

add_library(sonnet::world ALIAS sonnet_world)

target_include_directories(sonnet_world PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_world PUBLIC
    sonnet::core
    sonnet::api
    sonnet::loaders
)

sonnet_add_module_test(sonnet_world
    tests/TransformTests.cpp
    tests/SceneTests.cpp
    tests/CameraTests.cpp
)
