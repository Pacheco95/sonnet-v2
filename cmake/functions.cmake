function(sonnet_add_module_test MODULE_NAME)
    if(NOT SONNET_BUILD_TESTS)
        return()
    endif()

    set(SOURCES ${ARGN})
    set(TEST_NAME "${MODULE_NAME}_tests")

    add_executable(${TEST_NAME} ${SOURCES})

    target_include_directories(
        ${TEST_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(
        ${TEST_NAME}
        PRIVATE ${MODULE_NAME} Catch2::Catch2WithMain
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_target_properties(${TEST_NAME} PROPERTIES FOLDER "Tests")

    # Tag with the module name so `ctest -L <module>` runs just that module's
    # suite. The label is the second segment of "sonnet_<module>"; if the
    # caller passed a non-conformant target name, fall back to the full name.
    string(REGEX REPLACE "^sonnet_" "" _label "${MODULE_NAME}")
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS "${_label}")
endfunction()
