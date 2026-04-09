set(TARGET_NAME sonnet_input)

add_library(${TARGET_NAME}
    src/InputSystem.cpp
)
add_library(sonnet::input ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC include)

target_link_libraries(${TARGET_NAME} PUBLIC sonnet::api)

sonnet_add_module_test(${TARGET_NAME}
    tests/InputSystemTests.cpp
)
