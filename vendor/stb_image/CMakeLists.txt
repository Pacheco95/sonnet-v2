add_library(stb_image STATIC stb_image_impl.cpp)
add_library(stb::image ALIAS stb_image)

target_include_directories(stb_image PUBLIC ${stb_SOURCE_DIR})

# stb_image uses C idioms that trigger warnings — disable them for this target only.
set_target_properties(stb_image PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
target_compile_options(stb_image PRIVATE -w)
