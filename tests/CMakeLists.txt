# SPDX-License-Identifier: AGPL-3.0-or-later
set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/cpm" CACHE PATH "CPM download cache")
include(CPM)
CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.16.1 SYSTEM YES)
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)

# Tests of the plug-in layer (processor, parameters, state) link the plug-in's shared code; all others link
# subgen_dsp only.
set(plugin_test_names test_smoke test_params test_processor test_layouts test_state)
file(GLOB unit_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp")
set(dsp_test_sources)
set(plugin_test_sources)
foreach(file IN LISTS unit_tests)
    get_filename_component(name "${file}" NAME_WE)
    if(name IN_LIST plugin_test_names)
        list(APPEND plugin_test_sources "${file}")
    else()
        list(APPEND dsp_test_sources "${file}")
    endif()
endforeach()

# Test support (DFT of any length, measurements, test signals, golden data, allocation guard), compiled into each
# test executable; the allocation guard replaces the global operator new there.
add_library(subgen_test_support INTERFACE)
file(GLOB support_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")
target_sources(subgen_test_support INTERFACE ${support_sources})
target_include_directories(subgen_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(subgen_test_support INTERFACE SUBGEN_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_executable(subgen_dsp_tests ${dsp_test_sources})
target_link_libraries(subgen_dsp_tests
    PRIVATE subgen_dsp subgen_test_support Catch2::Catch2WithMain juce::juce_recommended_config_flags)
subgen_strict_warnings(subgen_dsp_tests)
catch_discover_tests(subgen_dsp_tests)

# Like JUCE's own plug-in wrappers: link the shared-code static library and re-export its include directories (its
# public compile definitions, JucePlugin_* and the module flags, come with the link).
if(SUBGEN_BUILD_PLUGIN)
    add_executable(subgen_plugin_tests ${plugin_test_sources})
    target_include_directories(subgen_plugin_tests PRIVATE $<TARGET_PROPERTY:SubGen,INCLUDE_DIRECTORIES>)
    target_link_libraries(subgen_plugin_tests PRIVATE SubGen subgen_test_support Catch2::Catch2WithMain)
    subgen_strict_warnings(subgen_plugin_tests)
    catch_discover_tests(subgen_plugin_tests)
endif()

# Documentation consistency (SPEC_BUILD_RELEASE §11): parameter registry, identifiers and test IDs.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME check_docs
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_docs.py" --docs "${PROJECT_SOURCE_DIR}/docs")
endif()
