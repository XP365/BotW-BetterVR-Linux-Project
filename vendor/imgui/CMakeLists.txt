cmake_minimum_required(VERSION 3.10)
project(imgui LANGUAGES CXX)

file(GLOB IMGUI_SOURCES "*.cpp" "*.h")

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
