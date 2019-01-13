cmake_minimum_required(VERSION 3.4)

include_directories(${test_include_dirs})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

function (in_tests_folder target)
	place_in_folder(${target} "tests/font_test")
endfunction()

add_compile_options(-march=native -Wall -fmax-errors=1)
add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

add_executable(console_test console_test.cpp essentials.h essentials.cpp)
target_link_libraries(console_test learnogl -lrt)
in_tests_folder(console_test)

add_executable(font_test font_test.cpp essentials.h essentials.cpp)
target_link_libraries(font_test learnogl -lrt)
in_tests_folder(font_test)

add_executable(input_system_test input_system_test.cpp essentials.h essentials.cpp)
target_compile_options(input_system_test PRIVATE -std=c++14)
target_link_libraries(input_system_test learnogl -lrt)
in_tests_folder(input_system_test)

add_executable(ui ui.cpp essentials.h essentials.cpp)
target_link_libraries(ui learnogl -lrt)
in_tests_folder(font_test)
