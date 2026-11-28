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

# Sources
set(INCREDIBLEHULK_SOURCES
    src/main.cpp
    src/crash_report.cpp
    src/native_gpu_autodiag.cpp
    src/assets_locator.cpp
    # Self-contained, native original EoR resource backend. No SDK dependency.
    src/eor_usa_index.cpp
    src/eor_verified_sampler_bindings.cpp
    src/eor_model_resource.cpp
    src/eor_model_material_refs.cpp
    src/eor_model_surfaces.cpp
    src/eor_native_mesh_geometry.cpp
    src/eor_native_attributes.cpp
    src/eor_native_mesh_upload.cpp
    src/eor_model_surface_materials.cpp
    src/eor_original_model_assets.cpp
    src/eor_original_texture_assets.cpp
    src/eor_original_texture_streams.cpp
    src/eor_original_texture_mip_ranges.cpp
    src/eor_original_texture_gpu_layout.cpp
    src/eor_source_mip_resolver.cpp
    src/eor_xenos_texture_mips.cpp
    src/eor_original_texture_usa.cpp
    src/eor_original_model_upload.cpp
    src/eor_native_world_stream.cpp
    src/eor_native_archive_source.cpp
    src/eor_font_pack.cpp
    src/eor_native_font_geometry.cpp
    src/eor_motion_vectors.cpp
    src/eor_native_frame.cpp
    src/eor_spirv_bank.cpp
    # Collision-free original Xbox virtual-header -> full source program
    # identity. No proprietary shader bytes or guest assets linked here.
    src/eor_original_virtual_shader_identity.cpp
    src/eor_native_shader_constants.cpp
    src/eor_spirv_texture_bindings.cpp
    src/eor_aot_draw_bridge.cpp
    src/eor_native_draw_geometry.cpp
    src/eor_xdk_draw_capture.cpp
    src/eor_ui_asset_catalog.cpp
    src/eor_ui_original_dds.cpp
    src/pc_menu.cpp
    # Existing game-side input/window hooks; no separate launcher is built.
    src/launcher/input_hook.cpp
    src/launcher/native_input_map.cpp
    src/launcher/native_input_win.cpp
    src/launcher/native_camera_direct.cpp
    src/launcher/return_to_launcher.cpp
    src/launcher/game_window.cpp
)

# Real host-side services: compiled once as a native static library.
# No SDK include directories, generated PPC, GPU plugin or guest runtime
# link dependencies are inherited by these translation units.
add_library(hulk_native_core STATIC
    src/native_game_data_root_store.cpp
    src/eor_archive.cpp
    src/eor_fmod_project.cpp
    src/eor_fmod_catalog.cpp
    src/eor_havok_packfile.cpp
    src/eor_frame_scheduler.cpp
    src/eor_native_lod.cpp
    src/native_pc_options_store.cpp
    src/native_pc_save_path.cpp
    src/native_pc_save_format.cpp
)
target_include_directories(hulk_native_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    # An actual Windows x64 audio device backend, isolated from guest PPC.
    # Its PCM input must come from a verified decoded original sound bank.
    target_compile_definitions(hulk_native_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_sources(hulk_native_core PRIVATE src/eor_native_audio_xaudio2.cpp)
    target_link_libraries(hulk_native_core PUBLIC shell32 ole32 xaudio2 mfplat mfuuid wmcodecdspuuid)
endif()

# Keep the single already-existing physical Vulkan host in one native
# library. The very same target can be compiled independently of the XEX,
# ReXGlue runtime and NativeX. No substitute renderer or second game EXE.
function(hulk_add_eor_vulkan_host_target)
    add_library(hulk_eor_win32_vulkan_host STATIC src/eor_win32_vulkan_host.cpp src/eor_vulkan_color_transfer.cpp)
    target_include_directories(hulk_eor_win32_vulkan_host PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hulk_eor_win32_vulkan_host PUBLIC
        Diligent-GraphicsEngineVk-static Diligent-GraphicsEngineVkInterface
        Diligent-Common PRIVATE volk::volk Vulkan::Headers)
endfunction()

# This is a host library build, not a second game executable or a release.
# It can be compiled and exercised without fetching any SDK or game bytes.
option(HULK_NATIVE_CORE_ONLY "Build native host services without the transitional runtime" OFF)
option(HULK_NATIVE_EOR_VULKAN_ONLY
    "Build the existing Win32 EoR/Vulkan device and swapchain without ReXGlue or NativeX" OFF)
if(HULK_NATIVE_EOR_VULKAN_ONLY)
    if(NOT WIN32)
        message(FATAL_ERROR "EoR Win32/Vulkan host target requires Windows")
    endif()
    # DiligentCore is the native Vulkan API backend, not a NativeX runtime.
    # Accept a standalone existing checkout; retain the old submodule only
    # as a backward-compatible source location, never as a linked renderer.
    set(HULK_NATIVE_DILIGENT_SOURCE_DIR "" CACHE PATH
        "Existing standalone DiligentCore checkout (no NativeX required)")
    if(HULK_NATIVE_DILIGENT_SOURCE_DIR)
        set(_hulk_diligent_source "${HULK_NATIVE_DILIGENT_SOURCE_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/diligent-core/CMakeLists.txt")
        set(_hulk_diligent_source "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/diligent-core")
    else()
        set(HULK_NATIVE_X_SOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/native_x_renderer" CACHE PATH
            "Legacy location for already-checked-out DiligentCore")
        set(_hulk_diligent_source "${HULK_NATIVE_X_SOURCE_DIR}/thirdparty/diligent-core")
    endif()
    if(NOT EXISTS "${_hulk_diligent_source}/CMakeLists.txt")
        message(FATAL_ERROR
            "Native Vulkan requires an existing DiligentCore checkout; "
            "pass -DHULK_NATIVE_DILIGENT_SOURCE_DIR=... (NativeX is not required)")
    endif()
    add_subdirectory("${_hulk_diligent_source}"
        "${CMAKE_CURRENT_BINARY_DIR}/hulk_native_eor_diligent")
    hulk_add_eor_vulkan_host_target()
    # Original EoR archives, models, materials, LOD, image decoding,
    # ShaderEffects, SPIR-V bank, source Draw and render-target chronology.
    # Every source is the SAME file used in the current game executable,
    # compiled without a generated PPC table, ReXGlue or NativeX.
    add_library(hulk_eor_native_resources STATIC
        src/eor_usa_index.cpp
        src/eor_verified_sampler_bindings.cpp
        src/eor_model_resource.cpp
        src/eor_model_material_refs.cpp
        src/eor_model_surfaces.cpp
        src/eor_native_mesh_geometry.cpp
        src/eor_native_attributes.cpp
        src/eor_native_mesh_upload.cpp
        src/eor_model_surface_materials.cpp
        src/eor_original_model_assets.cpp
        src/eor_original_texture_assets.cpp
        src/eor_original_texture_streams.cpp
        src/eor_original_texture_mip_ranges.cpp
        src/eor_original_texture_gpu_layout.cpp
        src/eor_source_mip_resolver.cpp
        src/eor_xenos_texture_mips.cpp
        src/eor_original_texture_usa.cpp
        src/eor_original_model_upload.cpp
        src/eor_native_world_stream.cpp
        src/eor_native_archive_source.cpp
        src/eor_font_pack.cpp
        src/eor_native_font_geometry.cpp
        src/eor_motion_vectors.cpp
        src/eor_native_frame.cpp
        src/eor_spirv_bank.cpp
        src/eor_original_virtual_shader_identity.cpp
        src/eor_native_shader_constants.cpp
        src/eor_spirv_texture_bindings.cpp
        src/eor_aot_draw_bridge.cpp
        src/eor_native_draw_geometry.cpp
        src/eor_xdk_draw_capture.cpp
        src/eor_ui_asset_catalog.cpp
        src/eor_ui_original_dds.cpp)
    target_include_directories(hulk_eor_native_resources PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hulk_eor_native_resources PUBLIC hulk_native_core)
    # Compile the actual existing native EoR DrawIndexed, SPIR-V/PSO,
    # textures, model buffers and intermediate Resolve pipeline here too.
    # This reuses SAME sources as the current game; it imports NO ReXGlue,
    # NativeX renderer device, Xenia or guest OS services. No second game
    # executable or automated release workflow is introduced.
    add_library(hulk_eor_native_gpu STATIC
        src/eor_diligent_mesh_buffers.cpp
        src/eor_diligent_scene_targets.cpp
        src/eor_diligent_xenos_textures.cpp
        src/eor_diligent_material_textures.cpp
        src/eor_diligent_spirv_shaders.cpp
        src/eor_diligent_original_pipeline.cpp
        src/eor_diligent_aot_backend.cpp
        src/eor_diligent_texture_residency.cpp
        src/eor_native_frame_executor.cpp)
    target_include_directories(hulk_eor_native_gpu PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hulk_eor_native_gpu PUBLIC
        hulk_eor_win32_vulkan_host hulk_eor_native_resources hulk_native_core
        PRIVATE volk::volk Vulkan::Headers)
    return()
endif()
if(HULK_NATIVE_CORE_ONLY)
    enable_testing()
    foreach(_test IN ITEMS
        native_pc_save_format_test
        native_pc_options_store_test
        native_game_data_root_store_test
        eor_fmod_project_test
        eor_fmod_catalog_test
        eor_native_audio_pcm_test
        eor_fsb4_pcm_test
        eor_fsb3_bank_test
        eor_fsb3_codec_test
        eor_fev_waveform_test
        eor_fev_event_table_test
        eor_fev_event_effect_test
        eor_voice_options_test
        eor_event_voice_group_test
        eor_static_dsp_test
        eor_dynamic_dsp_test
        eor_fev_automation_test
        eor_fev_routes_test
        eor_fev_voice_schedule_test
        eor_fev_seek_pcm_test
        eor_fmod_update_clock_test
        eor_fmod_start_trace_test
        eor_fmod_live_events_test
        eor_fmod_live_binding_test
        eor_fmod_live_cues_test
        eor_spatial_audio_test
        eor_fmod_live_spatial_test
        eor_fmod_live_mix_test
        eor_fmod_doppler_test
        eor_fmod_acoustics_test
        eor_live_dsp_stream_test
        eor_native_present_contract_test
        eor_exit_confirmation_test
        eor_resource_generation_test
        eor_havok_packfile_test
        eor_native_havok_scheduler_test
        eor_native_lod_test)
        add_executable(${_test} "${CMAKE_CURRENT_SOURCE_DIR}/../ci/${_test}.cpp")
        target_link_libraries(${_test} PRIVATE hulk_native_core)
        # Keep the existing assert-based regression checks active in Release.
        if(MSVC)
            target_compile_options(${_test} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${_test} PRIVATE -UNDEBUG)
        endif()
        if(_test STREQUAL "eor_live_dsp_stream_test")
            find_package(Threads REQUIRED)
            target_link_libraries(${_test} PRIVATE Threads::Threads)
        endif()
        add_test(NAME ${_test} COMMAND ${_test})
    endforeach()
    if(WIN32)
        add_executable(eor_mpeg_mft_test "${CMAKE_CURRENT_SOURCE_DIR}/../ci/eor_mpeg_mft_test.cpp")
        target_link_libraries(eor_mpeg_mft_test PRIVATE hulk_native_core)
        add_test(NAME eor_mpeg_mft_test COMMAND eor_mpeg_mft_test)
        # A Windows Server image can omit the optional Microsoft decoder.
        # Report that as skipped, never a successful decode.
        set_tests_properties(eor_mpeg_mft_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    return()
endif()

# Temporary generated-code and host compatibility integration. Native core
# above deliberately does not consume this package or its target settings.
include(generated/rexglue.cmake)

if(WIN32)
    # File details, and a manifest that makes the ANSI code page UTF-8 so accented paths work (playbook chapter 2).
    list(APPEND INCREDIBLEHULK_SOURCES src/incredible_hulk.rc src/incredible_hulk.manifest)
    add_executable(incredible_hulk WIN32 ${INCREDIBLEHULK_SOURCES})
    target_compile_definitions(incredible_hulk PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(incredible_hulk PRIVATE comdlg32 bcrypt shell32 ole32)
else()
    add_executable(incredible_hulk ${INCREDIBLEHULK_SOURCES})
endif()

target_link_libraries(incredible_hulk PRIVATE hulk_native_core)
rexglue_setup_target(incredible_hulk GPU_PLUGINS xenos)

# Native Vulkan renderer integration (NativeXRenderer + validated game-specific XDK profile).
# The bridge is real, unlike the old v0.2 link-only stub: profile loading,
# dynamic call binding and Initialize() are wired in. The GPU path cannot be
# enabled until Hulk's actual XDK 6534 layouts and entrypoints are known.
option(HULK_WITH_NATIVE_X "Compile native Vulkan renderer and runtime XDK hook bridge" ON)
# Never include the expensive per-XDK-hook PPC ABI profiler in a Release
# package unless a developer opts into this separate, nondefault build mode.
option(HULK_NATIVE_GPU_HOOK_PROBE "Compile developer-only Hulk XDK hook profiler" OFF)
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
    if(WIN32 AND TARGET incredible_hulk_recomp)
        # Run after every actual codegen; the patch is idempotent and rejects
        # unsupported translated instructions without changing original audio.
        add_custom_target(hulk_fmod_start_trace_codegen
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../ci/test_patch_hulk_fmod_start_trace.py"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../ci/patch_hulk_fmod_start_trace.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/generated/default" "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex"
            VERBATIM COMMENT "Verify and instrument original FMOD start-time selection")
        add_dependencies(hulk_fmod_start_trace_codegen incredible_hulk_codegen)
        add_dependencies(incredible_hulk_recomp hulk_fmod_start_trace_codegen)
    endif()
    set(_hulk_hooks "${CMAKE_CURRENT_BINARY_DIR}/generated/hulk_native_x_hooks.cpp")
    add_custom_command(OUTPUT "${_hulk_hooks}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_native_x_hooks.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/native_hook_addresses.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/analysis/guest_host_mapping.csv" "${_hulk_hooks}"
            "${CMAKE_CURRENT_SOURCE_DIR}/hulk_native_x_profile.ini"
            "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
        DEPENDS tools/gen_native_x_hooks.py native_hook_addresses.txt analysis/guest_host_mapping.csv
                hulk_native_x_profile.ini "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
        VERBATIM COMMENT "Generate guest-function NativeX hook wrappers")
    add_subdirectory("${HULK_NATIVE_X_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/hulk_native_x_renderer")
    target_sources(incredible_hulk PRIVATE src/eor_xdk_hook_bridge.cpp src/native_x_renderer_hulk.cpp
        src/native_x_hulk_profile.cpp "${_hulk_hooks}")
    if(HULK_NATIVE_GPU_HOOK_PROBE)
        target_sources(incredible_hulk PRIVATE src/native_gpu_hook_probe.cpp)
        target_compile_definitions(incredible_hulk PRIVATE HULK_NATIVE_GPU_HOOK_PROBE=1)
    endif()
    # Native EoR GPU buffers are immutable DiligentCore allocations, independent
    # of guest Xenos/PM4 emulation. Runtime creation needs an actual host device.
    target_sources(incredible_hulk PRIVATE src/eor_diligent_mesh_buffers.cpp
        src/eor_diligent_scene_targets.cpp src/eor_diligent_xenos_textures.cpp
        src/eor_diligent_material_textures.cpp src/eor_diligent_spirv_shaders.cpp
        src/eor_diligent_original_pipeline.cpp src/eor_diligent_aot_backend.cpp
        src/eor_diligent_texture_residency.cpp
        src/eor_diligent_host_output.cpp)
    target_link_libraries(incredible_hulk PRIVATE Diligent-GraphicsEngineVkInterface Diligent-Common
        volk::volk Vulkan::Headers)
    if(WIN32)
        # Existing physical host now builds as a separate native static
        # library in BOTH modes: no duplicated implementation or game EXE.
        hulk_add_eor_vulkan_host_target()
        target_link_libraries(incredible_hulk PRIVATE hulk_eor_win32_vulkan_host)
        target_sources(incredible_hulk PRIVATE src/eor_native_frame_executor.cpp)
    endif()
    target_link_libraries(incredible_hulk PRIVATE native_x_renderer::native_x_renderer)
    target_include_directories(incredible_hulk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${HULK_NATIVE_X_SOURCE_DIR}/src")
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
