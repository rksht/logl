cmake_minimum_required(VERSION 3.4)

add_compile_options(-march=native -fmax-errors=1 -Wall)
include_directories(${test_include_dirs})

function (in_tests_folder target)
	place_in_folder(${target} "tests/fb_test")
endfunction()

include_directories(${PROJECT_SOURCE_DIR}/include
					${PROJECT_SOURCE_DIR}/glfw/include
					${PROJECT_SOURCE_DIR}/glad/include
					${PROJECT_SOURCE_DIR}/glm
					${PROJECT_SOURCE_DIR}/assimp/include
					${PROJECT_SOURCE_DIR}/scaffold/include
					${PROJECT_SOURCE_DIR}/imgui
					${PROJECT_SOURCE_DIR}/test  # Need cwd.h
				   )

add_compile_options(-march=native)

add_executable(fb_test main.cpp dbg_imgui.cpp dbg_imgui.h)
target_link_libraries(fb_test scaffold learnogl glad glfw imgui -lGL)
in_tests_folder(fb_test)
