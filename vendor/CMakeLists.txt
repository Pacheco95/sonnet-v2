# Third-party code vendored directly into the repository.
# Dependencies fetched via FetchContent are declared in cmake/dependencies.cmake.

if(SONNET_USE_OPENGL)
    add_subdirectory(glad)
endif()
add_subdirectory(stb_image)
