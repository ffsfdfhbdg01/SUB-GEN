# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SUBGEN_VERSION LIMIT_COUNT 1)
project(SubGen VERSION ${SUBGEN_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SUBGEN_AAX "Also build the AAX format (manual CI job only)" OFF)
option(SUBGEN_BUILD_TESTS "Build the unit tests" ON)
set(SUBGEN_SANITIZE "" CACHE STRING "Sanitizers for the asan/tsan presets: address,undefined or thread")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProductIdentity)
include(Warnings)

if(SUBGEN_SANITIZE)
    add_compile_options(-fsanitize=${SUBGEN_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${SUBGEN_SANITIZE})
endif()

add_subdirectory(JUCE SYSTEM)

# Everything below the plug-in layer. An INTERFACE library: the sources compile into each consumer (the plug-in's
# shared code and subgen_dsp_tests), so the tests run exactly the code the plug-in ships.
add_library(subgen_dsp INTERFACE)
file(GLOB_RECURSE subgen_dsp_sources CONFIGURE_DEPENDS
    src/core/*.cpp src/dsp/*.cpp src/engines/*.cpp src/chain/*.cpp src/routing/*.cpp src/analysis/*.cpp)
if(subgen_dsp_sources)
    target_sources(subgen_dsp INTERFACE ${subgen_dsp_sources})
endif()
target_include_directories(subgen_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(subgen_dsp INTERFACE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0) # no network (SPEC §3)
target_link_libraries(subgen_dsp INTERFACE juce::juce_core juce::juce_audio_basics juce::juce_dsp)

set(subgen_formats VST3 AU Standalone)
if(SUBGEN_AAX)
    list(APPEND subgen_formats AAX)
endif()

juce_add_plugin(SubGen
    PRODUCT_NAME "${SUBGEN_PRODUCT_NAME}"
    COMPANY_NAME "${SUBGEN_VENDOR}"
    BUNDLE_ID "${SUBGEN_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE "${SUBGEN_MANUFACTURER_CODE}"
    PLUGIN_CODE "${SUBGEN_PLUGIN_CODE}"
    VERSION "${SUBGEN_VERSION}"
    FORMATS ${subgen_formats}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD TRUE)

file(GLOB_RECURSE subgen_plugin_sources CONFIGURE_DEPENDS src/params/*.cpp src/plugin/*.cpp)
if(subgen_plugin_sources)
    target_sources(SubGen PRIVATE ${subgen_plugin_sources})
endif()
target_compile_definitions(SubGen PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SubGen
    PRIVATE subgen_dsp juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)
subgen_strict_warnings(SubGen)

if(SUBGEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
