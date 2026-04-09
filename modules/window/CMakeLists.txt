set(TARGET_NAME sonnet_window)

add_library(${TARGET_NAME}
    src/GLFWInputAdapter.cpp
    src/GLFWWindow.cpp
)
add_library(sonnet::window ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC include)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        sonnet::api
        glfw
)

sonnet_add_module_test(${TARGET_NAME}
    tests/GLFWInputAdapterTests.cpp
)
