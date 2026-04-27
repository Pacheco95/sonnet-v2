add_library(sonnet_scene STATIC
    src/SceneLoader.cpp
)

add_library(sonnet::scene ALIAS sonnet_scene)

target_include_directories(sonnet_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_scene
    PUBLIC  sonnet::core
            sonnet::world
    PRIVATE sonnet::api
            sonnet::loaders
            sonnet::physics
            sonnet::primitives
            sonnet::renderer::frontend
            nlohmann_json::nlohmann_json
)

sonnet_add_module_test(sonnet_scene
    tests/SceneLoaderTests.cpp
)

target_link_libraries(sonnet_scene_tests PRIVATE
    nlohmann_json::nlohmann_json
    sonnet::renderer::frontend
)

# Reuse the recording mocks defined alongside the api tests so SceneLoader
# can exercise its renderer-driven asset paths without a GPU context.
target_include_directories(sonnet_scene_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/api/tests
)
