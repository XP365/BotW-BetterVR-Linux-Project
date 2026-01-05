# ImPlot3D minimal CMake
cmake_minimum_required(VERSION 3.10)
project(implot3d LANGUAGES CXX)

# Add all source files
file(GLOB IMPL3D_SOURCES "*.cpp" "*.h")

# Create a static library
add_library(implot3d STATIC ${IMPL3D_SOURCES})

# Make sure it can find ImPlot and ImGui
target_include_directories(implot3d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../implot
    ../imgui
)

# Optionally, link dependencies if needed
target_link_libraries(implot3d PUBLIC implot imgui)
