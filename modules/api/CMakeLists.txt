set(TARGET_NAME sonnet_api)

add_library(${TARGET_NAME} INTERFACE)
add_library(sonnet::api ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} INTERFACE include)

target_link_libraries(${TARGET_NAME} INTERFACE sonnet::core)
