cmake_minimum_required(VERSION 3.4)

include(extra_functions)

if (MSVC)
else()
  add_compile_options(-march=native -fmax-errors=1 -Wall -rdynamic)
endif()

function (in_tests_folder target)
	place_in_folder(${target} "tests/game_1_test")
endfunction()

find_package(Threads)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)

add_subdirectory(liquidfun/liquidfun/Box2D)
# add_subdirectory(liquidfun/freeglut)
# add_subdirectory(liquidfun/googletest)

include_directories(${test_include_dirs})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/liquidfun/liquidfun/Box2D")

add_compile_options("-DG1_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

add_executable(b2d_tryout b2d_tryout.cpp b2d_tryout_meshes.inc.h)
target_link_libraries(b2d_tryout learnogl Box2D)

add_executable(typed_gl_test typed_gl_test.cpp)
target_link_libraries(typed_gl_test learnogl)
