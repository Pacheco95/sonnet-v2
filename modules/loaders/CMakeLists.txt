add_library(sonnet_loaders STATIC
    src/ShaderLoader.cpp
    src/TextureLoader.cpp
    src/ModelLoader.cpp
)

add_library(sonnet::loaders ALIAS sonnet_loaders)

target_include_directories(sonnet_loaders PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_loaders
    PUBLIC  sonnet::api
    PRIVATE stb::image
            assimp::assimp
)

sonnet_add_module_test(sonnet_loaders
    tests/BoundsTests.cpp
)
