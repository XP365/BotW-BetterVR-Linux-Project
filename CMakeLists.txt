cmake_minimum_required(VERSION 3.27)
project(BetterVR_Layer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Check architecture support
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x64 architecture is supported")
endif ()

# Set output/install directories
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/Cemu")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Set global options
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --- System libraries ---
# GLM (header-only)
include_directories(/usr/include/glm)

# Vulkan headers
include_directories(/usr/include/vulkan)

# OpenXR headers
include_directories(/usr/include/openxr)

# --- Vendor libraries ---
add_subdirectory(vendor/imgui)
add_subdirectory(vendor/implot)
add_subdirectory(vendor/implot3d)

# --- DLL / Shared library target ---
add_library(BetterVR_Layer SHARED)

# Precompiled header
target_precompile_headers(BetterVR_Layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h)

# Include directories
target_include_directories(BetterVR_Layer BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(BetterVR_Layer AFTER PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(BetterVR_Layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies)

# Add source files
target_sources(BetterVR_Layer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/instance.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/d3d12_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/vulkan_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/logger.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/update_checker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/update_checker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/framebuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/framebuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/layer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/cemu_hooks.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/weapon.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/weapon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/controls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/entity_debugger.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/entity_debugger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/rumble.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/rumble.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hooking/skeleton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/d3d12.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/d3d12.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/openxr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/openxr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/swapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/swapchain.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/vulkan.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/vulkan_imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui_impl_vulkan.cpp
)

# Add graphic pack files
file(GLOB_RECURSE GRAPHIC_PACK_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/BreathOfTheWild_BetterVR/*")
if(GRAPHIC_PACK_FILES)
    foreach(GRAPHIC_PACK_FILE ${GRAPHIC_PACK_FILES})
        file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/BreathOfTheWild_BetterVR" "${GRAPHIC_PACK_FILE}")
        string(REPLACE "\\" "/" REL_PATH "${REL_PATH}")
        source_group("BreathOfTheWild_BetterVR" FILES "${GRAPHIC_PACK_FILE}")
        set_source_files_properties("${GRAPHIC_PACK_FILE}" PROPERTIES HEADER_FILE_ONLY TRUE)
        list(APPEND GRAPHIC_PACK_HEADER_FILES "${GRAPHIC_PACK_FILE}")
    endforeach()
    target_sources(BetterVR_Layer PRIVATE ${GRAPHIC_PACK_HEADER_FILES})
endif()

# --- Compile definitions / flags ---
target_compile_definitions(BetterVR_Layer PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

# --- Link system libraries ---
target_link_libraries(BetterVR_Layer PRIVATE vulkan openxr)
target_link_libraries(BetterVR_Layer PRIVATE imgui implot implot3d)

# --- Install rules ---
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR LAUNCH CEMU IN VR.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR UNINSTALL.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR LAUNCH CEMU IN VR - COMPATIBILITY MODE.bat"
    DESTINATION "${CMAKE_INSTALL_PREFIX}"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR_Layer.json" DESTINATION "${CMAKE_INSTALL_PREFIX}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/BreathOfTheWild_BetterVR" DESTINATION "${CMAKE_INSTALL_PREFIX}/graphicPacks")
install(TARGETS BetterVR_Layer DESTINATION "${CMAKE_INSTALL_PREFIX}")

install(FILES $<TARGET_PDB_FILE:BetterVR_Layer> DESTINATION "${CMAKE_INSTALL_PREFIX}" OPTIONAL)
