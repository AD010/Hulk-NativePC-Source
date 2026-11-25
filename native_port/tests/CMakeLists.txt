cmake_minimum_required(VERSION 3.25)
project(HulkNativeInputTests LANGUAGES CXX)
enable_testing()
add_executable(hulk_input_test native_input_map_test.cpp ../src/launcher/native_input_map.cpp)
target_compile_features(hulk_input_test PRIVATE cxx_std_23)
add_test(NAME native_input_map COMMAND hulk_input_test)

add_executable(hulk_native_profile_test native_profile_test.cpp ../src/native_x_hulk_profile.cpp)
target_compile_features(hulk_native_profile_test PRIVATE cxx_std_23)
target_include_directories(hulk_native_profile_test PRIVATE
    ../src ../native_x_renderer_git/include)
add_test(NAME native_profile COMMAND hulk_native_profile_test)

add_executable(hulk_direct_camera_test direct_camera_test.cpp ../src/launcher/native_camera_direct.cpp)
target_compile_features(hulk_direct_camera_test PRIVATE cxx_std_23)
add_test(NAME direct_camera COMMAND hulk_direct_camera_test)

# Native PC save-path policy only; actual Xbox->PC serializer is a separate gate.
add_executable(hulk_native_save_path_test
    native_save_path_test.cpp ../src/native_pc_save_path.cpp)
target_compile_features(hulk_native_save_path_test PRIVATE cxx_std_23)
if(WIN32)
    target_link_libraries(hulk_native_save_path_test PRIVATE shell32 ole32)
endif()
add_test(NAME native_save_folder_policy COMMAND hulk_native_save_path_test)

# Offscreen integration against the same Diligent checkout as the port.
# Private USA files are supplied at test time and are never copied to the build.
if(HULK_DILIGENT_CORE)
    enable_language(C)
    add_subdirectory("${HULK_DILIGENT_CORE}" diligent EXCLUDE_FROM_ALL)
    add_executable(hulk_original_ui_vulkan_test eor_vulkan_original_ui_test.cpp
        ../src/eor_archive.cpp ../src/eor_usa_index.cpp ../src/eor_ui_original_dds.cpp
        ../src/eor_original_virtual_shader_identity.cpp
        ../src/eor_original_texture_usa.cpp ../src/eor_original_texture_assets.cpp
        ../src/eor_original_texture_gpu_layout.cpp ../src/eor_original_texture_mip_ranges.cpp
        ../src/eor_original_texture_streams.cpp ../src/eor_xenos_texture_mips.cpp
        ../src/eor_verified_sampler_bindings.cpp ../src/eor_diligent_texture_residency.cpp
        ../src/eor_diligent_xenos_textures.cpp ../src/eor_diligent_spirv_shaders.cpp
        ../src/eor_diligent_original_pipeline.cpp ../src/eor_diligent_material_textures.cpp
        ../src/eor_diligent_scene_targets.cpp ../src/eor_diligent_aot_backend.cpp
        ../src/eor_diligent_mesh_buffers.cpp ../src/eor_native_mesh_upload.cpp
        ../src/eor_native_shader_constants.cpp ../src/eor_spirv_texture_bindings.cpp
        ../src/eor_native_frame.cpp ../src/eor_aot_draw_bridge.cpp ../src/eor_spirv_bank.cpp ../src/eor_native_font_geometry.cpp
        ../src/eor_native_attributes.cpp ../src/eor_xdk_draw_capture.cpp)
    target_compile_features(hulk_original_ui_vulkan_test PRIVATE cxx_std_23)
    target_include_directories(hulk_original_ui_vulkan_test PRIVATE ../src)
    target_link_libraries(hulk_original_ui_vulkan_test PRIVATE
        Diligent-GraphicsEngineVk-static Diligent-GraphicsEngineVkInterface Diligent-Common)
    add_test(NAME original_mixed_targets_vulkan COMMAND hulk_original_ui_vulkan_test --mixed-targets)
    add_executable(hulk_original_pipeline_key_test eor_original_pipeline_key_test.cpp)
    target_compile_features(hulk_original_pipeline_key_test PRIVATE cxx_std_20)
    target_link_libraries(hulk_original_pipeline_key_test PRIVATE Diligent-GraphicsEngineVkInterface Diligent-Common)
    add_test(NAME original_pipeline_key COMMAND hulk_original_pipeline_key_test)
    # Formal SPIRV-Tools validation independent from driver acceptance.
    # These private source tests deliberately do not require a separate workflow.
    if(HULK_USA_SPIRV)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        find_program(HULK_SPIRV_VAL NAMES spirv-val spirv-val.exe REQUIRED)
        add_executable(hulk_export_cube_gamma eor_export_cube_gamma.cpp)
        target_compile_features(hulk_export_cube_gamma PRIVATE cxx_std_20)
        add_test(NAME original_shader_bank_spirv_val
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/validate_original_shader_bank.py"
                "${HULK_USA_SPIRV}" "$<TARGET_FILE:hulk_export_cube_gamma>" "${HULK_SPIRV_VAL}")
    endif()

    if(HULK_USA_SPIRV AND HULK_USA_EFFECTS)
        add_test(NAME full_original_shader_bank_vulkan COMMAND hulk_original_ui_vulkan_test
            --shader-bank "${HULK_USA_SPIRV}" "${HULK_USA_EFFECTS}")
    endif()
    if(HULK_USA_SPIRV AND HULK_USA_EFFECTS AND HULK_USA_REFLECTION)
        add_test(NAME original_material_replay_vulkan COMMAND hulk_original_ui_vulkan_test
            --material-replay "${HULK_USA_SPIRV}" "${HULK_USA_EFFECTS}" "${HULK_USA_REFLECTION}")
    endif()
    if(HULK_USA_INDEX AND HULK_USA_TEXTURES)
        add_test(NAME original_ui_vulkan COMMAND hulk_original_ui_vulkan_test
            "${HULK_USA_INDEX}" "${HULK_USA_TEXTURES}")
        if(HULK_USA_SPIRV AND HULK_USA_EFFECTS)
            add_test(NAME original_font_shader_vulkan COMMAND hulk_original_ui_vulkan_test
                "${HULK_USA_INDEX}" "${HULK_USA_TEXTURES}" "${HULK_USA_SPIRV}" "${HULK_USA_EFFECTS}")
            if(HULK_USA_FONT_GLYPHS)
                add_test(NAME original_captured_glyphs_vulkan COMMAND hulk_original_ui_vulkan_test
                    "${HULK_USA_INDEX}" "${HULK_USA_TEXTURES}" "${HULK_USA_SPIRV}"
                    "${HULK_USA_EFFECTS}" "${HULK_USA_FONT_GLYPHS}")
                if(HULK_USA_FONT_FETCH)
                    add_test(NAME original_captured_glyph_sampler_vulkan COMMAND hulk_original_ui_vulkan_test
                        "${HULK_USA_INDEX}" "${HULK_USA_TEXTURES}" "${HULK_USA_SPIRV}"
                        "${HULK_USA_EFFECTS}" "${HULK_USA_FONT_GLYPHS}" "${HULK_USA_FONT_FETCH}")
                    if(HULK_USA_REFLECTION AND HULK_USA_VIRTUAL_IDS AND HULK_USA_SHADER_DIR)
                        add_test(NAME original_staged_glyph_replay_vulkan COMMAND hulk_original_ui_vulkan_test
                            "${HULK_USA_INDEX}" "${HULK_USA_TEXTURES}" "${HULK_USA_SPIRV}" "${HULK_USA_EFFECTS}"
                            "${HULK_USA_FONT_GLYPHS}" "${HULK_USA_FONT_FETCH}" "${HULK_USA_REFLECTION}"
                            "${HULK_USA_VIRTUAL_IDS}" "${HULK_USA_SHADER_DIR}")
                    endif()
                endif()
            endif()
        endif()
    endif()
endif()

add_executable(hulk_original_resolve_reuse_test ../../ci/eor_original_resolve_reuse_test.cpp)
target_compile_features(hulk_original_resolve_reuse_test PRIVATE cxx_std_20)
add_test(NAME original_resolve_reuse COMMAND hulk_original_resolve_reuse_test)

add_executable(hulk_immutable_buffer_cache_test ../../ci/eor_immutable_buffer_cache_test.cpp)
target_compile_features(hulk_immutable_buffer_cache_test PRIVATE cxx_std_20)
add_test(NAME immutable_buffer_cache COMMAND hulk_immutable_buffer_cache_test)

# The actual original EFontShader DrawVertices / DrawIndexedVertices source
# can supply native float4 vertex streams when its current XDK declaration,
# pass, and atlas identity have been authenticated. Synthetic regression
# requires no original game files, Vulkan SDK or separate CI workflow.
add_executable(hulk_original_font_draw_test
    ../../ci/eor_native_font_geometry_test.cpp
    ../src/eor_native_font_geometry.cpp
    ../src/eor_native_attributes.cpp
    ../src/eor_xdk_draw_capture.cpp)
target_compile_features(hulk_original_font_draw_test PRIVATE cxx_std_23)
target_include_directories(hulk_original_font_draw_test PRIVATE ../src)
add_test(NAME original_font_source_draw COMMAND hulk_original_font_draw_test)
