cmake_minimum_required(VERSION 3.24)
project(ModelViewer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# A cached archive is checked against the same hash as a fresh download.
function(viewer_dependency name url sha)
    set(archive "${CMAKE_SOURCE_DIR}/build/downloads/${name}.tar.gz")
    if(EXISTS "${archive}")
        set(url "${archive}")
    endif()
    FetchContent_Declare(${name} URL "${url}" URL_HASH "SHA256=${sha}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR viewer-no-cmake-project)
endfunction()

viewer_dependency(sdl https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.4.0
    9614b9696abc4597ffce6b888829dc6537ae500423474c342ac4a67222c5654c)
viewer_dependency(imgui https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.5
    0eb50fe9aeba1a51f96b5843c7f630a32ed2e9362d693c61b87e4fa870cf826d)
viewer_dependency(glm https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1
    9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)
viewer_dependency(cgltf https://codeload.github.com/jkuhlmann/cgltf/tar.gz/refs/tags/v1.15
    84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a)
viewer_dependency(stb https://codeload.github.com/nothings/stb/tar.gz/f0569113c93ad095470c54bf34a17b36646bbbb5
    4d05c96640ae3a8cbdafdad8d344b50ce610802f78aee80154acdb8e266282e0)
viewer_dependency(ufbx https://codeload.github.com/ufbx/ufbx/tar.gz/refs/tags/v0.17.1
    8f57e97b10c98a0226d4022db7c32e0ad69a431e45a25017d5b046e5cb4dbab2)

viewer_dependency(assimp https://codeload.github.com/assimp/assimp/tar.gz/refs/tags/v6.0.2
    d1822d9a19c9205d6e8bc533bf897174ddb360ce504680f294170cc1d6319751)

set(SDL_SHARED OFF CACHE BOOL "")
set(SDL_STATIC ON CACHE BOOL "")
set(SDL_TEST_LIBRARY OFF CACHE BOOL "")
set(SDL_DISABLE_INSTALL ON CACHE BOOL "")
foreach(feature AUDIO CAMERA GPU RENDER JOYSTICK HAPTIC SENSOR VULKAN METAL OPENGLES)
    set(SDL_${feature} OFF CACHE BOOL "")
endforeach()
set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "")
set(ASSIMP_BUILD_3DS_IMPORTER ON CACHE BOOL "")
set(ASSIMP_NO_EXPORT ON CACHE BOOL "")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "")
set(ASSIMP_INSTALL OFF CACHE BOOL "")
if(APPLE)
    set(ASSIMP_BUILD_ZLIB OFF)
else()
    set(ASSIMP_BUILD_ZLIB ON)
endif()
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "")
FetchContent_MakeAvailable(sdl imgui glm cgltf stb ufbx assimp)
add_subdirectory("${sdl_SOURCE_DIR}" "${sdl_BINARY_DIR}" EXCLUDE_FROM_ALL)

add_subdirectory("${assimp_SOURCE_DIR}" "${assimp_BINARY_DIR}" EXCLUDE_FROM_ALL)
if(WIN32)
    # Assimp's bundled poly2tri uses std::ostream without including its header.
    if(MSVC)
        target_compile_options(assimp PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/FIostream>")
    else()
        target_compile_options(assimp PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include;ostream>")
    endif()
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_library(viewer_ui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(viewer_ui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(viewer_ui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS)
target_link_libraries(viewer_ui PRIVATE SDL3::SDL3-static OpenGL::GL)

add_library(viewer_model STATIC src/model.cpp src/gltf.cpp src/fbx.cpp src/three_ds.cpp "${ufbx_SOURCE_DIR}/ufbx.c")
target_link_libraries(viewer_model PRIVATE assimp::assimp)
target_include_directories(viewer_model SYSTEM
    PUBLIC "${glm_SOURCE_DIR}"
    PRIVATE "${ufbx_SOURCE_DIR}" "${cgltf_SOURCE_DIR}" "${stb_SOURCE_DIR}")

add_executable(ModelViewer WIN32 MACOSX_BUNDLE src/main.cpp src/renderer.cpp src/blend.cpp)
target_link_libraries(ModelViewer PRIVATE viewer_model viewer_ui SDL3::SDL3-static OpenGL::GL Threads::Threads)
target_compile_definitions(ModelViewer PRIVATE GL_SILENCE_DEPRECATION NOMINMAX)
if(MSVC)
    target_compile_options(ModelViewer PRIVATE /W4 /utf-8)
    target_compile_options(viewer_model PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8>")
else()
    target_compile_options(ModelViewer PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(viewer_model PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
endif()

set_target_properties(ModelViewer PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/script/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.modelviewer.app"
    MACOSX_BUNDLE_BUNDLE_NAME "Model Viewer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

install(TARGETS ModelViewer BUNDLE DESTINATION . RUNTIME DESTINATION .)
set(license_dir "licenses")
if(APPLE)
    set(license_dir "ModelViewer.app/Contents/Resources/licenses")
endif()
install(FILES "${sdl_SOURCE_DIR}/LICENSE.txt" DESTINATION "${license_dir}" RENAME SDL.txt)
install(FILES "${imgui_SOURCE_DIR}/LICENSE.txt" DESTINATION "${license_dir}" RENAME Dear-ImGui.txt)
install(FILES "${glm_SOURCE_DIR}/copying.txt" DESTINATION "${license_dir}" RENAME GLM.txt)
install(FILES "${cgltf_SOURCE_DIR}/LICENSE" DESTINATION "${license_dir}" RENAME cgltf.txt)
install(FILES "${stb_SOURCE_DIR}/LICENSE" DESTINATION "${license_dir}" RENAME stb.txt)
install(FILES "${ufbx_SOURCE_DIR}/LICENSE" DESTINATION "${license_dir}" RENAME ufbx.txt)
install(FILES "${assimp_SOURCE_DIR}/LICENSE" DESTINATION "${license_dir}" RENAME Assimp.txt)
install(FILES "${assimp_SOURCE_DIR}/contrib/zlib/LICENSE" DESTINATION "${license_dir}" RENAME zlib.txt)
