# incredible_hulk - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(incredible_hulk LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

# Windows file version for the game executable.
set(HULK_VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(INCREDIBLEHULK_SOURCES
    src/main.cpp
    src/crash_report.cpp
    src/assets_locator.cpp
    # Existing game-side input/window hooks; no separate launcher is built.
    src/launcher/input_hook.cpp
    src/launcher/native_input_map.cpp
    src/launcher/native_input_win.cpp
    src/launcher/native_camera_direct.cpp
    src/launcher/return_to_launcher.cpp
    src/launcher/game_window.cpp
)

if(WIN32)
    # File details, and a manifest that makes the ANSI code page UTF-8 so accented paths work (playbook chapter 2).
    list(APPEND INCREDIBLEHULK_SOURCES src/incredible_hulk.rc src/incredible_hulk.manifest)
    add_executable(incredible_hulk WIN32 ${INCREDIBLEHULK_SOURCES})
    target_link_libraries(incredible_hulk PRIVATE comdlg32 bcrypt)
else()
    add_executable(incredible_hulk ${INCREDIBLEHULK_SOURCES})
endif()

rexglue_setup_target(incredible_hulk GPU_PLUGINS xenos)

# Native Vulkan renderer integration (NativeXRenderer + validated game-specific XDK profile).
# The bridge is real, unlike the old v0.2 link-only stub: profile loading,
# dynamic call binding and Initialize() are wired in. The GPU path cannot be
# enabled until Hulk's actual XDK 6534 layouts and entrypoints are known.
option(HULK_WITH_NATIVE_X "Compile native Vulkan renderer and runtime XDK hook bridge" ON)
set(HULK_NATIVE_X_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/native_x_renderer" CACHE PATH "Full renderer source")
if(HULK_WITH_NATIVE_X)
    if(NOT REXSDK_DIR OR NOT EXISTS "${REXSDK_DIR}/RECOMP.md")
        message(FATAL_ERROR "NativeX requires the ElGuslml/rexglue-sdk recomp branch")
    endif()
    if(NOT EXISTS "${HULK_NATIVE_X_SOURCE_DIR}/thirdparty/diligent-core/CMakeLists.txt")
        message(FATAL_ERROR "NativeX DiligentCore submodule is absent")
    endif()
    if(HULK_XDK_PM4_PROBE)
        message(FATAL_ERROR "HULK_XDK_PM4_PROBE conflicts with native dynamic hooks; use one mode at a time")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_hulk_hooks "${CMAKE_CURRENT_BINARY_DIR}/generated/hulk_native_x_hooks.cpp")
    add_custom_command(OUTPUT "${_hulk_hooks}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_native_x_hooks.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/native_hook_addresses.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/analysis/guest_host_mapping.csv" "${_hulk_hooks}"
        DEPENDS tools/gen_native_x_hooks.py native_hook_addresses.txt analysis/guest_host_mapping.csv
        VERBATIM COMMENT "Generate guest-function NativeX hook wrappers")
    add_subdirectory("${HULK_NATIVE_X_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/hulk_native_x_renderer")
    target_sources(incredible_hulk PRIVATE src/native_x_renderer_hulk.cpp
        src/native_x_hulk_profile.cpp src/native_gpu_hook_probe.cpp "${_hulk_hooks}")
    target_link_libraries(incredible_hulk PRIVATE native_x_renderer::native_x_renderer)
    target_include_directories(incredible_hulk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    native_x_renderer_copy_runtime(incredible_hulk)
    target_compile_definitions(incredible_hulk PRIVATE HULK_NATIVE_X_LINKED=1)

    if(WIN32)
        # rc.exe searches its current work directory and /I paths; a relative
        # RCDATA path in src/incredible_hulk.rc fails from Ninja build dirs.
        # Generate a separate Windows resource with a deterministic absolute
        # forward-slash filename. No extra INI is shipped beside the EXE.
        file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/hulk_native_x_profile.ini" _hulk_profile_asset)
        set(_hulk_profile_resource "${CMAKE_CURRENT_BINARY_DIR}/hulk_native_profile.rc")
        file(WRITE "${_hulk_profile_resource}" "3001 RCDATA \"${_hulk_profile_asset}\"\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/hulk_native_x_profile.ini")
        set_source_files_properties("${_hulk_profile_resource}" PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hulk_native_x_profile.ini")
        target_sources(incredible_hulk PRIVATE "${_hulk_profile_resource}")
    endif()
endif()

# PM4 address evidence only; this never redirects drawing. opt-in developer tool.
option(HULK_XDK_PM4_PROBE "Log limited hits at candidate XDK PM4 builders" OFF)
if(HULK_XDK_PM4_PROBE)
    target_sources(incredible_hulk PRIVATE src/xdk_pm4_probe.cpp)
endif()

# Read-only diagnostics: Xbox controller pollers identified in the supplied recomp EXE.
# These probes ONLY log and call through. They do not provide direct mouse/camera input.
option(HULK_GAME_INPUT_PROBE "Trace controller polling callsites (developer only)" OFF)
if(HULK_GAME_INPUT_PROBE)
    target_sources(incredible_hulk PRIVATE src/game_input_probe.cpp)
endif()

string(REPLACE "." "," HULK_VERSION_COMMAS "${HULK_VERSION}")
target_compile_definitions(incredible_hulk PRIVATE
    HULK_VERSION="${HULK_VERSION}" HULK_VERSION_COMMAS=${HULK_VERSION_COMMAS},0
    # Developer aids (HULK_DEV_PAD, HULK_DUMP_IMAGE) only outside release builds.
    $<$<NOT:$<CONFIG:Release>>:HULK_DEV_TOOLS>)

# A link map of every build, kept with each release (release-maps/<version>) to read crash addresses later.
if(WIN32)
    target_link_options(incredible_hulk PRIVATE "LINKER:/MAP:${CMAKE_CURRENT_BINARY_DIR}/incredible_hulk.map")
endif()
