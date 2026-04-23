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

if(SONNET_USE_VULKAN)
    target_link_libraries(${TARGET_NAME} PUBLIC Vulkan::Vulkan)
    # Must be on the include search path *before* any <GLFW/glfw3.h> include
    # in the TU, which is why it's set at CMake level rather than in GLFWWindow.h
    # (GLFWInputAdapter.h pulls in GLFW first, so a header-level #define is too
    # late once pragma once has suppressed re-inclusion).
    target_compile_definitions(${TARGET_NAME} PUBLIC GLFW_INCLUDE_VULKAN)
endif()

sonnet_add_module_test(${TARGET_NAME}
    tests/GLFWInputAdapterTests.cpp
)
