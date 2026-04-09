set(TARGET_NAME "glad")

add_library(${TARGET_NAME} STATIC)

target_sources(${TARGET_NAME} PRIVATE src/glad.c)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
