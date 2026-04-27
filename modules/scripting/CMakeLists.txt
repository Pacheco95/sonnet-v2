add_library(sonnet_scripting
    src/LuaScriptRuntime.cpp
)
add_library(sonnet::scripting ALIAS sonnet_scripting)

target_include_directories(sonnet_scripting PUBLIC include)

target_link_libraries(sonnet_scripting
    PUBLIC  sonnet_world
            sonnet_input
    PRIVATE lua_static
            sol2::sol2
            glm::glm
)

# sol2 generates a lot of warnings we don't own; silence them.
target_compile_options(sonnet_scripting PRIVATE -w)

sonnet_add_module_test(sonnet_scripting
    tests/LuaScriptRuntimeTests.cpp
)
