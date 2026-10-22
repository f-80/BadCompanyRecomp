# badcompanyrecomp - Battlefield: Bad Company PC Recompilation
# Based on ReXGlue SDK v0.10.0+

cmake_minimum_required(VERSION 3.25)
project(badcompanyrecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(BADCO_RELEASE_PACKAGE "Use quiet release defaults with pinned SDK binaries" OFF)

list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/rexglue/win-amd64")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    include(generated/rexglue.cmake)
endif()
# The code-generation executable has only a Release import in this SDK.
if(BADCO_RELEASE_PACKAGE AND TARGET rex::rexglue)
    set_property(TARGET rex::rexglue PROPERTY MAP_IMPORTED_CONFIG_RELEASE Release)
endif()

# Sources
# LLVM 18.1.8 crashes selecting x86 instructions for sub_822C2EC8 at /O2.
# Keep the generated module unchanged; use a local per-source workaround.
set_property(SOURCE generated/default/badcompanyrecomp_recomp.82.cpp
    APPEND PROPERTY COMPILE_OPTIONS /Od)
set_source_files_properties(generated/default/badcompanyrecomp_recomp.82.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(BADCOMPANYRECOMP_SOURCES
    src/main.cpp
    src/spdlog_compat.cpp
    src/trial_dispatch_trace.cpp
)

if(WIN32)
    add_executable(badcompanyrecomp WIN32 ${BADCOMPANYRECOMP_SOURCES})
    target_link_libraries(badcompanyrecomp PRIVATE dbghelp)
    target_link_options(badcompanyrecomp PRIVATE
        "/MAP:${CMAKE_CURRENT_BINARY_DIR}/badcompanyrecomp.map")
else()
    add_executable(badcompanyrecomp ${BADCOMPANYRECOMP_SOURCES})
endif()

if(BADCO_RELEASE_PACKAGE)
    target_compile_definitions(badcompanyrecomp PRIVATE BADCO_RELEASE_PACKAGE=1)
    if(WIN32)
        target_link_options(badcompanyrecomp PRIVATE /DEBUG:FULL "/PDBALTPATH:%_PDB%")
    endif()
endif()

if(COMMAND rexglue_setup_target)
    rexglue_setup_target(badcompanyrecomp GPU_PLUGINS xenos)
endif()
if(BADCO_RELEASE_PACKAGE AND WIN32 AND TARGET rex::TracyClient)
    # The pinned runtime imports Tracy even though Release host profiling is off.
    add_custom_command(TARGET badcompanyrecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rex::TracyClient> $<TARGET_FILE_DIR:badcompanyrecomp>)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/settings/gamecontrollerdb.txt")
    add_custom_command(TARGET badcompanyrecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/settings/gamecontrollerdb.txt"
                "$<TARGET_FILE_DIR:badcompanyrecomp>/gamecontrollerdb.txt"
        COMMENT "Copying gamecontrollerdb.txt next to badcompanyrecomp.exe")
endif()
