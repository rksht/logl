cmake_minimum_required(VERSION 3.4)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(stbi SHARED stbi.c)
set_target_properties(stbi PROPERTIES PREFIX "")

# add_executable(gl_id_to_name gl_id_to_name.cpp)
