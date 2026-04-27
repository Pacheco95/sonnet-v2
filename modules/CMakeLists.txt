# Engine modules — added here in dependency order as they are implemented.

# Coverage instrumentation: compile flag is scoped here so third-party code
# under vendor/ and FetchContent _deps/ is not instrumented and does not
# pollute the report. The link flag lives at root scope so every executable
# that links engine objects (including apps/demo) gets the gcov runtime.
if(SONNET_COVERAGE)
    add_compile_options(--coverage -O0 -g)
endif()

add_subdirectory(core)
add_subdirectory(api)
add_subdirectory(input)
add_subdirectory(window)
add_subdirectory(loaders)
add_subdirectory(world)
add_subdirectory(physics)
add_subdirectory(scripting)
add_subdirectory(renderer)
add_subdirectory(primitives)
add_subdirectory(scene)
add_subdirectory(ui)
