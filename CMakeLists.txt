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
include(cmake/dependencies.cmake)
include(cmake/functions.cmake)

enable_testing()

add_subdirectory(vendor)
add_subdirectory(modules)
add_subdirectory(apps)
