cmake_minimum_required(VERSION 3.4)

include_directories(${test_include_dirs})

add_compile_options(-march=native -fmax-errors=1)

function (in_tests_folder target)
	place_in_folder(${target} "tests/bv_test")
endfunction()

suppress_all_msvc_warnings()

add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"" "-DDXERR_LOGURU")
add_compile_options("-DRESOURCES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")

# Enable Blend2D static build.
set(BLEND2D_STATIC TRUE)
set(BLEND2D_DIR "${CMAKE_CURRENT_LIST_DIR}/blend2d" CACHE PATH "Location of 'blend2d'")

include("${BLEND2D_DIR}/CMakeLists.txt")

add_executable(cos_distance cos_distance.cpp)
target_link_libraries(cos_distance learnogl Blend2D::Blend2D)
in_tests_folder(cos_distance)

add_executable(b2d b2d.cpp)
target_link_libraries(b2d Blend2D::Blend2D)
in_tests_folder(cos_distance)
