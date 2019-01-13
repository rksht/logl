cmake_minimum_required(VERSION 3.4)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
include(extra_functions)

add_compile_options(-march=native -fmax-errors=1 -Wall -Wextra -Wpedantic)

include_directories(${test_include_dirs})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader)

add_executable(tiny tiny.cpp image.h idk.h tinyobjloader/tiny_obj_loader.h tinyobjloader/tiny_obj_loader.cc)
target_link_libraries(tiny learnogl scaffold)
target_compile_definitions(tiny PRIVATE -DMESH_FILE="${CMAKE_CURRENT_SOURCE_DIR}/suzanne.obj")

add_library(loadobj SHARED loadobj.cpp)
set_target_properties(loadobj PROPERTIES PREFIX "")
target_link_libraries(loadobj learnogl-shared)
