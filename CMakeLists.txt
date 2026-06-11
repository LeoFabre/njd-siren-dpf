cmake_minimum_required(VERSION 3.22)
project(Siren VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIREN_BUILD_UI         "Build the NanoVG faceplate UI"          ON)
option(SIREN_BUILD_TESTS      "Build offline DSP tests"                ON)
option(SIREN_BUILD_STANDALONE "Build standalone app (UI dev/testing)" OFF)

add_subdirectory(dpf)

set(SIREN_TARGETS vst3 lv2)
if(SIREN_BUILD_STANDALONE)
    list(APPEND SIREN_TARGETS jack)
endif()

if(SIREN_BUILD_UI)
    dpf_add_plugin(Siren
        TARGETS ${SIREN_TARGETS}
        UI_TYPE opengl
        FILES_DSP plugin/SirenPlugin.cpp
        FILES_UI  plugin/SirenUI.cpp)
    target_compile_definitions(Siren PUBLIC DISTRHO_PLUGIN_HAS_UI=1)
else()
    dpf_add_plugin(Siren
        TARGETS vst3 lv2
        FILES_DSP plugin/SirenPlugin.cpp)
    target_compile_definitions(Siren PUBLIC DISTRHO_PLUGIN_HAS_UI=0)
endif()

target_include_directories(Siren PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp)

# MinGW builds must not depend on libgcc/libstdc++/winpthread DLLs — hosts on
# machines without MSYS2 would silently fail to load the plugin.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(t Siren-vst3 Siren-lv2 Siren-jack)
        if(TARGET ${t})
            target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endforeach()
endif()

if(SIREN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
