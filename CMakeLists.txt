cmake_minimum_required(VERSION 3.22)
project(Siren VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIREN_BUILD_TESTS "Build offline DSP tests" ON)

add_subdirectory(dpf)

dpf_add_plugin(Siren
    TARGETS vst3 lv2
    FILES_DSP plugin/SirenPlugin.cpp)

target_include_directories(Siren PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp)

if(SIREN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
