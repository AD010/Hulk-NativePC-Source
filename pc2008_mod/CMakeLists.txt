cmake_minimum_required(VERSION 3.21)
project(Hulk2008PC_Enhancement LANGUAGES CXX)
if(NOT WIN32 OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This optional plugin requires Win32/x86: original HULK.EXE is PE32.")
endif()
add_library(HulkPC SHARED src/HulkPC.cpp)
set_target_properties(HulkPC PROPERTIES PREFIX "" OUTPUT_NAME "HulkPC"
                      SUFFIX ".asi" CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
target_compile_definitions(HulkPC PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    set_property(TARGET HulkPC PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(HulkPC PRIVATE /W3 /EHsc)
endif()
