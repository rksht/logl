cmake_minimum_required(VERSION 3.4)

include(extra_functions)

add_compile_options(-march=native -fmax-errors=1)
include_directories(${test_include_dirs})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

function (in_tests_folder target)
	place_in_folder(${target} "tests/math_test")
endfunction()

# add_executable(eigensolver_test eigensolver_test.cpp)
# target_link_libraries(eigensolver_test learnogl)

# add_executable(basic_test basic_test.cpp)
# target_link_libraries(basic_test learnogl)

add_executable(ortho_comp_test ortho_comp_test.cpp)
target_link_libraries(ortho_comp_test learnogl)
in_tests_folder(ortho_comp_test)

add_compile_options(-march=native)
add_executable(matmul_bench_test matmul_bench_test.cpp)
target_link_libraries(matmul_bench_test)
target_include_directories(matmul_bench_test PRIVATE ${PROJECT_SOURCE_DIR}/third/scaffold/bench/benchmark/include)
target_link_libraries(matmul_bench_test benchmark)
in_tests_folder(matmul_bench_test)
