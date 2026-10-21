# SPDX-License-Identifier: AGPL-3.0-or-later
# Warnings as errors on our own sources only (src/, tests/). JUCE, Catch2 and the other dependencies keep their own
# flags; their headers are SYSTEM includes, so they do not trip these warnings either.

# subgen_strict_warnings(<target>): sets the strict flags on every source file under src/ and tests/ in the
# directory scope of <target> (source file properties are per directory, and the DSP sources reach the test
# targets through the subgen_dsp INTERFACE library).
function(subgen_strict_warnings target)
    file(GLOB_RECURSE own_sources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/*.cpp"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp")
    if(NOT own_sources)
        return()
    endif()
    if(MSVC)
        set(flags /W4 /WX)
    else()
        set(flags -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_source_files_properties(${own_sources} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS "${flags}")
endfunction()
