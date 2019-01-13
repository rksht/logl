cmake_minimum_required(VERSION 3.4)

include_directories(${test_include_dirs})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
else()
  add_compile_options(-march=native -fmax-errors=1)
endif()
add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

function(in_tests_folder target)
  place_in_folder(${target} "tests/ibl_test")
endfunction()

add_executable(ibl_intro ibl_intro.cpp ibl_intro_utils.h)
target_link_libraries(ibl_intro learnogl)
in_tests_folder(ibl_intro)
