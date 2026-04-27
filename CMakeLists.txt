cmake_minimum_required(VERSION 3.25)

project(sonnet VERSION 0.1.0 LANGUAGES CXX C)

if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build directory.")
endif()

include(cmake/options.cmake)

if(SONNET_USE_VULKAN)
    add_compile_definitions(SONNET_USE_VULKAN=1)
endif()
if(SONNET_USE_OPENGL)
    add_compile_definitions(SONNET_USE_OPENGL=1)
endif()

include(cmake/dependencies.cmake)
include(cmake/functions.cmake)

# Coverage link flag must be visible to every executable that links engine
# objects, including apps/demo. Compile flag is added inside modules/ so
# third-party code is not instrumented. Compiler-id sanity check lives here.
if(SONNET_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "SONNET_COVERAGE requires GCC or Clang (got ${CMAKE_CXX_COMPILER_ID}).")
    endif()
    add_link_options(--coverage)
endif()

enable_testing()

add_subdirectory(vendor)
add_subdirectory(modules)
add_subdirectory(apps)

# Coverage report target. Builds, runs ctest, then runs gcovr against the
# engine modules' object directories. Requires SONNET_COVERAGE=ON and gcovr
# on PATH (pip install gcovr). Output: <build>/coverage/index.html.
#
# Clang and GCC emit incompatible .gcno formats. When building with clang we
# tell gcovr to drive `llvm-cov gcov` instead of the system gcov.
if(SONNET_COVERAGE)
    find_program(GCOVR_EXECUTABLE gcovr)
    set(_gcov_args "")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Match the clang version so the .gcno format matches the runtime.
        find_program(LLVM_COV_EXECUTABLE
            NAMES "llvm-cov-${CMAKE_CXX_COMPILER_VERSION}"
                  "llvm-cov-${CMAKE_MATCH_1}"
                  llvm-cov)
        if(LLVM_COV_EXECUTABLE)
            set(_gcov_args --gcov-executable "${LLVM_COV_EXECUTABLE} gcov")
        endif()
    endif()

    if(GCOVR_EXECUTABLE)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${GCOVR_EXECUTABLE}
                    --root ${CMAKE_SOURCE_DIR}
                    --filter ${CMAKE_SOURCE_DIR}/modules/
                    --exclude .*/tests/.*
                    --exclude-throw-branches
                    --print-summary
                    --html-details ${CMAKE_BINARY_DIR}/coverage/index.html
                    --xml ${CMAKE_BINARY_DIR}/coverage/coverage.xml
                    ${_gcov_args}
                    ${CMAKE_BINARY_DIR}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests and generating coverage report at ${CMAKE_BINARY_DIR}/coverage/index.html"
            VERBATIM
            USES_TERMINAL)
    else()
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E echo
                    "gcovr not found. Install with: pip install gcovr"
            COMMAND ${CMAKE_COMMAND} -E false)
    endif()
endif()
