set(TARGET_NAME sonnet_core)

add_library(${TARGET_NAME} INTERFACE)
add_library(sonnet::core ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} INTERFACE include)

target_link_libraries(${TARGET_NAME} INTERFACE glm::glm spdlog::spdlog)

sonnet_add_module_test(${TARGET_NAME}
    tests/StoreTests.cpp
)
