cmake_minimum_required(VERSION 3.4)

include(extra_functions)

add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

if (NOT MSVC)
  add_compile_options(-march=native -fmax-errors=1 -Wall -rdynamic)
endif()

if (${MSVC})
  add_compile_options(-W0)
endif()

function (in_tests_folder target)
	place_in_folder(${target} "tests/shadow_maps_test")
endfunction()

include_directories(${test_include_dirs})

add_executable(shadowmap_test shadowmap_test.cpp shadow_map.h shadow_map.cpp demo_types.h demo_types.cpp)
if (LOGL_FSANITIZE_MEMORY)
  target_link_libraries(shadowmap_test -lasan learnogl)
else()
  target_link_libraries(shadowmap_test learnogl)
endif()
in_tests_folder(shadowmap_test)

add_executable(thick_glass thick_glass.cpp shadow_map.h shadow_map.cpp demo_types.h demo_types.cpp)
if (LOGL_FSANITIZE_MEMORY)
  target_link_libraries(thick_glass -lasan learnogl)
else()
  target_link_libraries(thick_glass learnogl)
endif()
in_tests_folder(thick_glass)

add_executable(image_based_test image_based_test.cpp shadow_map.h shadow_map.cpp demo_types.h demo_types.cpp imgui_gl3_render.inc.h imgui_glfw_input.inc.h
				imgui_impl_opengl3.cpp imgui_impl_opengl3.h imgui_impl_glfw.cpp imgui_impl_glfw.h)
if (LOGL_FSANITIZE_MEMORY)
  target_link_libraries(image_based_test -lasan learnogl)
else()
  target_link_libraries(image_based_test learnogl_compat)
endif()
in_tests_folder(image_based_test)

add_executable(shadow_map_test_new_api shadow_map_test_new_api.cpp shadow_map.h shadow_map.cpp demo_types.h demo_types.cpp)
  target_link_libraries(shadow_map_test_new_api learnogl)
in_tests_folder(shadowmap_test)
