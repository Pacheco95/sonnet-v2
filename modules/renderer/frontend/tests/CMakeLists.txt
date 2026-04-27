if(NOT SONNET_BUILD_TESTS)
    return()
endif()

set(TEST_NAME sonnet_renderer_frontend_tests)

add_executable(${TEST_NAME}
    UboLayoutsTests.cpp
    RendererTests.cpp
)

# MockBackends.h lives alongside the api tests; renderer tests reuse it rather
# than duplicating the recording-mock harness.
target_include_directories(${TEST_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/api/tests
)

target_link_libraries(${TEST_NAME} PRIVATE
    sonnet::renderer::frontend
    Catch2::Catch2WithMain
)

add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_target_properties(${TEST_NAME} PROPERTIES FOLDER "Tests")
