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
        ../src/eor_archive.cpp ../src/eor_usa_index.cpp
        ../src/eor_original_virtual_shader_identity.cpp
        ../src/eor_original_texture_usa.cpp ../src/eor_original_texture_assets.cpp
        ../src/eor_original_texture_gpu_layout.cpp ../src/eor_original_texture_mip_ranges.cpp
        ../src/eor_original_texture_streams.cpp ../src/eor_xenos_texture_mips.cpp
        ../src/eor_verified_sampler_bindings.cpp ../src/eor_diligent_texture_residency.cpp
        ../src/eor_diligent_xenos_textures.cpp ../src/eor_diligent_spirv_shaders.cpp
        ../src/eor_diligent_original_pipeline.cpp ../src/eor_diligent_material_textures.cpp
        ../src/eor_diligent_scene_targets.cpp ../src/eor_spirv_bank.cpp ../src/eor_native_font_geometry.cpp
        ../src/eor_native_attributes.cpp ../src/eor_xdk_draw_capture.cpp)
    target_compile_features(hulk_original_ui_vulkan_test PRIVATE cxx_std_23)
    target_include_directories(hulk_original_ui_vulkan_test PRIVATE ../src)
    target_link_libraries(hulk_original_ui_vulkan_test PRIVATE
        Diligent-GraphicsEngineVk-static Diligent-GraphicsEngineVkInterface Diligent-Common)
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
                endif()
            endif()
        endif()
    endif()
endif()

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
