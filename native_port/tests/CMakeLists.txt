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
