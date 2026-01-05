set(VCPKG_TARGET_TRIPLET "x64-windows-static")
set(VCPKG_HOST_TRIPLET "x64-windows-static")

cmake_minimum_required(VERSION 3.27.0)
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
set(CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD 1)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY CMAKE_VS_INCLUDE_INSTALL_TO_DEFAULT_BUILD 1)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BetterVR_Layer)

# Set compilation options
add_compile_definitions(NOMINMAX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:EditAndContinue>$<$<CONFIG:RelWithDebInfo>:ProgramDatabase>")

# Find vcpkg dependencies
find_path(VULKAN_HEADERS_INCLUDE_DIRS "vk_video/vulkan_video_codec_h264std.h")
find_package(OpenXR CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(implot3d CONFIG REQUIRED)

# Add DLL target
add_library(BetterVR_Layer SHARED)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:BetterVR_Layer>/BetterVR_Layer.json" INPUT "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR_Layer.json")

# Set VS Debugger Command
set_target_properties(BetterVR_Layer PROPERTIES
    VS_DEBUGGER_COMMAND "${CMAKE_INSTALL_PREFIX}/Cemu_relwithdebinfo.exe"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    VS_DEBUGGER_ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_CREMENTIF_bettervr\nVK_LAYER_PATH=$<TARGET_FILE_DIR:BetterVR_Layer>/;"
)

# Add some flags
add_link_options(BetterVR_Layer PUBLIC "$<$<CONFIG:Debug>:/INCREMENTAL>")

# Add (precompiled) headers for DLL
target_precompile_headers(BetterVR_Layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h)
target_include_directories(BetterVR_Layer BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(BetterVR_Layer AFTER PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add source for DLL
target_sources(BetterVR_Layer PUBLIC
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
)

# Add graphic pack files to Visual Studio for editing
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

# Add vcpkg dependencies for DLL
target_link_libraries(BetterVR_Layer PRIVATE OpenXR::headers OpenXR::openxr_loader)
target_include_directories(BetterVR_Layer SYSTEM PRIVATE ${VULKAN_HEADERS_INCLUDE_DIRS})
target_link_libraries(BetterVR_Layer PRIVATE glm::glm)
target_link_libraries(BetterVR_Layer PRIVATE imgui::imgui)
target_link_libraries(BetterVR_Layer PRIVATE implot::implot)
target_link_libraries(BetterVR_Layer PRIVATE implot3d::implot3d)


# Add manual dependencies for DLL
target_compile_definitions(BetterVR_Layer PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_sources(BetterVR_Layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui_impl_vulkan.cpp)
target_include_directories(BetterVR_Layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies)

# Set install rules
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR LAUNCH CEMU IN VR.bat" "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR UNINSTALL.bat" "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR LAUNCH CEMU IN VR - COMPATIBILITY MODE.bat" DESTINATION "${CMAKE_INSTALL_PREFIX}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/BetterVR_Layer.json" DESTINATION "${CMAKE_INSTALL_PREFIX}")
#install(CODE "file(REMOVE_RECURSE \"${CMAKE_INSTALL_PREFIX}/graphicPacks/BreathOfTheWild_BetterVR\")")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/BreathOfTheWild_BetterVR" DESTINATION "${CMAKE_INSTALL_PREFIX}/graphicPacks")
install(TARGETS BetterVR_Layer DESTINATION "${CMAKE_INSTALL_PREFIX}")
install(FILES $<TARGET_PDB_FILE:BetterVR_Layer> DESTINATION "${CMAKE_INSTALL_PREFIX}" OPTIONAL)