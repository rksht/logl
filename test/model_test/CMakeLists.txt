cmake_minimum_required(VERSION 3.4)

include(extra_functions)

march_native()
one_error_max()
add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

function (in_tests_folder target)
	place_in_folder(${target} "tests/model_test")
endfunction()

include_directories(${test_include_dirs})

add_executable(arcball_test arcball.cpp)
target_link_libraries(arcball_test learnogl)
in_tests_folder(arcball_test)

add_executable(tangent_space_calc tangent_space_calc.cpp)
target_link_libraries(tangent_space_calc learnogl)
target_compile_options(tangent_space_calc
	BEFORE PRIVATE -DMODEL_FILE="${PROJECT_SOURCE_DIR}/test/models/suzanne.obj")
in_tests_folder(tangent_space_calc)

add_executable(draw_principle_axis draw_principle_axis.cpp)
target_link_libraries(draw_principle_axis learnogl)
in_tests_folder(draw_principle_axis)

add_executable(principle_axis_test principle_axis_test.cpp dbg_imgui.cpp)
target_link_libraries(principle_axis_test learnogl)
in_tests_folder(principle_axis_test)

add_executable(generate_points generate_points.cpp dbg_imgui.cpp)
target_link_libraries(generate_points learnogl)
in_tests_folder(generate_points)
