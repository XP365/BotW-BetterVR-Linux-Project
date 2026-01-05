cmake_minimum_required(VERSION 3.10)
project(implot LANGUAGES CXX)

file(GLOB IMPLOT_SOURCES "*.cpp" "*.h")

add_library(implot STATIC ${IMPLOT_SOURCES})

target_include_directories(implot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../imgui
)

target_link_libraries(implot PUBLIC imgui)
