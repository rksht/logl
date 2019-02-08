cmake_minimum_required(VERSION 3.4)

include_directories(${test_include_dirs})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
else()
  add_compile_options(-march=native)
endif()
add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

function(in_tests_folder target)
  place_in_folder(${target} "tests/defer_test")
endfunction()

add_executable(
  non_defer_test non_defer_test.cpp essentials.h essentials.cpp par_shapes.cpp)
target_link_libraries(non_defer_test learnogl)
in_tests_folder(non_defer_test)

add_executable(defer_test_quad
               defer_test_quad.cpp
               essentials.h
               essentials.cpp
               par_shapes.cpp)
target_link_libraries(defer_test_quad learnogl)
in_tests_folder(defer_test_quad)

add_executable(defer_light_test
               defer_light_test.cpp
               essentials.h
               essentials.cpp
               par_shapes.cpp)
target_link_libraries(defer_light_test learnogl)
in_tests_folder(defer_light_test)

add_executable(occlude_then_defer
               occlude_then_defer.cpp
               essentials.h
               essentials.cpp
               par_shapes.cpp)
target_link_libraries(occlude_then_defer learnogl)
in_tests_folder(occlude_then_defer)

add_executable(debug_overlay_test
               debug_overlay_test.cpp
               essentials.h
               essentials.cpp)
target_link_libraries(debug_overlay_test learnogl)
in_tests_folder(debug_overlay_test)

add_executable(
  generate_spheres generate_spheres.cpp essentials.h essentials.cpp par_shapes.cpp)
target_link_libraries(generate_spheres learnogl)
in_tests_folder(generate_spheres)

add_executable(generate_lights
               generate_lights.cpp
               essentials.h
               essentials.cpp
               par_shapes.cpp)
target_link_libraries(generate_lights learnogl)
in_tests_folder(generate_lights)

add_executable(order_dependent
               order_dependent.cpp
               essentials.h
               essentials.cpp
               par_shapes.cpp)
target_link_libraries(order_dependent learnogl)
in_tests_folder(order_dependent)

add_executable(order_independent
               order_independent.cpp
               essentials.h
               essentials.cpp)
target_link_libraries(order_independent learnogl)
in_tests_folder(order_independent)

add_executable(with_typed_gl_render
               with_typed_gl_test.cpp)
target_link_libraries(with_typed_gl_render learnogl)
target_compile_options(with_typed_gl_render BEFORE PUBLIC
  -DWITH_TYPED_TEST_RENDER=1)
in_tests_folder(with_typed_gl_render)

add_executable(with_typed_gl_generate
               with_typed_gl_test.cpp)
target_link_libraries(with_typed_gl_generate learnogl)
target_compile_options(with_typed_gl_generate BEFORE PUBLIC
  -DWITH_TYPED_TEST_GENERATE=1)
in_tests_folder(with_typed_gl_generate)
