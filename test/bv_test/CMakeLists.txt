cmake_minimum_required(VERSION 3.4)

include_directories(${test_include_dirs})

add_compile_options(-march=native -fmax-errors=1)

function (in_tests_folder target)
	place_in_folder(${target} "tests/bv_test")
endfunction()

suppress_all_msvc_warnings()

add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"" "-DDXERR_LOGURU")
add_compile_options("-DRESOURCES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")

add_executable(string_table_test string_table_test.cpp)
target_link_libraries(string_table_test learnogl)
in_tests_folder(string_table_test)

add_executable(file_monitor_test file_monitor_test.cpp)
target_link_libraries(file_monitor_test learnogl)
in_tests_folder(file_monitor_test)

add_executable(timed_block_test timed_block_test.cpp)
target_link_libraries(timed_block_test learnogl)
in_tests_folder(timed_block_test)

add_executable(box_test box_test.cpp)
target_link_libraries(box_test learnogl)
in_tests_folder(box_test)

add_executable(vmath_test vmath_test.cpp)
target_link_libraries(vmath_test learnogl)
in_tests_folder(vmath_test)

add_executable(cons_test cons_test.cpp)
target_link_libraries(cons_test learnogl)
in_tests_folder(cons_test)

add_executable(json_validator_test json_validator_test.cpp)
target_link_libraries(json_validator_test learnogl)
in_tests_folder(json_validator_test)

add_subdirectory(d3d_dev)

add_executable(fluidcs11 fluidcs11.cpp imgui_gl3_render.inc.h imgui_glfw_input.inc.h)
target_link_libraries(fluidcs11 learnogl imgui)
in_tests_folder(fluidcs11)

add_executable(use_shaderc_test use_shaderc_test.cpp)
target_link_libraries(use_shaderc_test learnogl)

add_executable(simple_sc_test simple_sc_test.cpp)
target_link_libraries(simple_sc_test -lasan learnogl)
in_tests_folder(simple_sc_test)

add_executable(duk_tryout duk_tryout.cpp)
target_link_libraries(duk_tryout learnogl)

add_executable(stams_simple_fluid stams_simple_fluid.cpp)
target_link_libraries(stams_simple_fluid learnogl)

add_executable(raycast raycast.cpp)
target_link_libraries(raycast learnogl)

add_executable(raycast_2 raycast_2.cpp)
target_link_libraries(raycast_2 learnogl)

add_executable(once_block_test once_block_test.cpp)
target_link_libraries(once_block_test learnogl)
