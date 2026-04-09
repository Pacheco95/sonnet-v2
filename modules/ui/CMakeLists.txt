add_library(sonnet_ui STATIC
    src/ImGuiLayer.cpp
)

add_library(sonnet::ui ALIAS sonnet_ui)

target_include_directories(sonnet_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sonnet_ui
    PUBLIC  sonnet::core
    PRIVATE imgui::imgui
)
