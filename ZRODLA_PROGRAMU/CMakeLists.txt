cmake_minimum_required(VERSION 3.20)
project(MebelStudio3D_Cpp VERSION 0.16 LANGUAGES CXX)

# Build the existing production C++ components. The top-level .cpp files are
# incomplete migration prototypes; they are retained as references and must not
# replace the Python editor. Building these targets does not migrate the GUI.
# Example (PowerShell, with the compiler directory also added to PATH):
# cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release `
#   -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
#   -DCMAKE_RC_COMPILER=C:/msys64/ucrt64/bin/windres.exe `
#   -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe
# cmake --build build-native
# ctest --test-dir build-native --output-on-failure
# Outputs stay in build-native/bin; the installed application is not modified.

if(NOT WIN32)
    message(FATAL_ERROR "The existing native components require Windows x64.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native DLL ABI and bundled application require a 64-bit compiler.")
endif()
enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(ms3d_native_options target)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    if(NOT "${target}" STREQUAL "MebelStudio3D")
        target_compile_definitions(${target} PRIVATE NOMINMAX)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8>")
    else()
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wno-missing-field-initializers>")
        if(MINGW)
            target_link_options(${target} PRIVATE -static -static-libstdc++ -static-libgcc)
        endif()
    endif()
endfunction()

# The full application host loads the bundled Python 3.13 runtime and the
# unchanged editor modules; runtime/app staging is handled by the package build.
add_executable(MebelStudio3D WIN32 native/host/host.cpp native/host/host.rc)
target_include_directories(MebelStudio3D PRIVATE native/host)
target_link_libraries(MebelStudio3D PRIVATE shell32)
ms3d_native_options(MebelStudio3D)
if(MINGW)
    target_compile_options(MebelStudio3D PRIVATE "$<$<COMPILE_LANGUAGE:RC>:--codepage=65001>")
    target_link_options(MebelStudio3D PRIVATE -municode)
elseif(MSVC)
    target_compile_options(MebelStudio3D PRIVATE "$<$<COMPILE_LANGUAGE:RC>:/c65001>")
    target_link_options(MebelStudio3D PRIVATE /MANIFEST:NO)
endif()

add_library(MebelModel SHARED
    native/core/model_api.cpp
    native/core/core_model.cpp
    native/core/shapes.cpp
    native/core/construction.cpp
    native/core/services.cpp
    native/core/document.cpp
    native/core/layout.cpp
    native/core/layout_geometry.cpp
    native/core/worktop.cpp)
set_target_properties(MebelModel PROPERTIES PREFIX "")
ms3d_native_options(MebelModel)

set(MS3D_RENDERER_SOURCES
    native/src/scene.cpp
    native/src/renderer_d3d11.cpp
    native/src/renderer_d3d12.cpp
    native/src/renderer_vulkan.cpp)
set(MS3D_RENDERER_LIBRARIES
    d3d11 d3d12 dxgi gdi32 gdiplus user32 ole32)
if(MINGW)
    list(APPEND MS3D_RENDERER_LIBRARIES d3dcompiler_47)
else()
    list(APPEND MS3D_RENDERER_LIBRARIES d3dcompiler)
endif()

add_library(MebelNative3D SHARED native/src/native_embed.cpp ${MS3D_RENDERER_SOURCES})
set_target_properties(MebelNative3D PROPERTIES PREFIX "")
target_include_directories(MebelNative3D PRIVATE native/third_party/vulkan/include)
target_link_libraries(MebelNative3D PRIVATE ${MS3D_RENDERER_LIBRARIES})
ms3d_native_options(MebelNative3D)

# The standalone viewer is read-only; it is not the full furniture editor.
option(MS3D_BUILD_SCENE_VIEWER "Build the existing read-only scene viewer" OFF)
if(MS3D_BUILD_SCENE_VIEWER)
    add_executable(MebelSceneViewer WIN32 native/src/main.cpp ${MS3D_RENDERER_SOURCES})
    target_include_directories(MebelSceneViewer PRIVATE native/third_party/vulkan/include)
    target_link_libraries(MebelSceneViewer PRIVATE ${MS3D_RENDERER_LIBRARIES} comdlg32 shell32)
    ms3d_native_options(MebelSceneViewer)
    if(MINGW)
        target_link_options(MebelSceneViewer PRIVATE -municode)
    endif()
    add_custom_command(TARGET MebelSceneViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/fixtures"
            "$<TARGET_FILE_DIR:MebelSceneViewer>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
            "$<TARGET_FILE_DIR:MebelSceneViewer>/runtime"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/reports")
    add_executable(test_scene_native native/tests/test_scene_native.cpp native/src/scene.cpp)
    target_include_directories(test_scene_native PRIVATE native/src)
    ms3d_native_options(test_scene_native)
    add_test(NAME native_scene COMMAND test_scene_native
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/fixtures"
        "${CMAKE_BINARY_DIR}/reports/native_scene_tests.json")

    add_executable(test_model_native native/tests/test_model_native.cpp)
    ms3d_native_options(test_model_native)
    if(MINGW)
        target_link_options(test_model_native PRIVATE -municode)
    endif()
    add_dependencies(test_model_native MebelModel)
    add_test(NAME native_model COMMAND test_model_native
        "$<TARGET_FILE:MebelModel>"
        "${CMAKE_BINARY_DIR}/reports/native_model_tests.json")
    add_executable(test_document_native native/tests/test_document_native.cpp)
    ms3d_native_options(test_document_native)
    if(MINGW)
        target_link_options(test_document_native PRIVATE -municode)
    endif()
    add_dependencies(test_document_native MebelModel)
    add_test(NAME native_document COMMAND test_document_native
        "$<TARGET_FILE:MebelModel>"
        "${CMAKE_BINARY_DIR}/reports/native_document_tests.json")
    add_executable(test_layout_native native/tests/test_layout_native.cpp)
    ms3d_native_options(test_layout_native)
    if(MINGW)
        target_link_options(test_layout_native PRIVATE -municode)
    endif()
    add_dependencies(test_layout_native MebelModel)
    add_test(NAME native_layout COMMAND test_layout_native
        "$<TARGET_FILE:MebelModel>"
        "${CMAKE_BINARY_DIR}/reports/native_layout_tests.json")
    add_executable(test_worktop native/tests/test_worktop.cpp)
    ms3d_native_options(test_worktop)
    if(MINGW)
        target_link_options(test_worktop PRIVATE -municode)
    endif()
    add_dependencies(test_worktop MebelModel)
    add_test(NAME native_worktop COMMAND test_worktop
        "$<TARGET_FILE:MebelModel>"
        "${CMAKE_BINARY_DIR}/reports/native_worktop_tests.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/fixtures/worktop_round_cases.json")
    set_tests_properties(native_scene native_model native_document native_layout native_worktop PROPERTIES TIMEOUT 120)
endif()

message(STATUS "Building native application host, model and viewport; the complete editor still uses Python.")
