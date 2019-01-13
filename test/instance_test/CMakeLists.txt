cmake_minimum_required(VERSION 3.4)

include_directories(${test_include_dirs})

function (in_tests_folder target)
	place_in_folder(${target} "tests/instance_test")
endfunction()

add_compile_options(-march=native -DSOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(instance_test main.cpp)
target_link_libraries(instance_test scaffold learnogl imgui glad glfw -ldl -lGL)
in_tests_folder(instance_test)

# add_executable(gen_image gen_image.cpp)
# target_link_libraries(gen_image scaffold learnogl)


add_executable(particles_test particles.cpp inc.h essentials.h)
target_link_libraries(particles_test scaffold learnogl glad glfw -ldl -lGL)
in_tests_folder(particles_test)

add_executable(instance_test_separate main_separate_formats.cpp)
target_link_libraries(instance_test_separate scaffold learnogl imgui glad glfw -ldl -lGL)
in_tests_folder(instance_test_separate)
