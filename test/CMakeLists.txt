cmake_minimum_required(VERSION 3.4)

include(extra_functions)

# Include dirs for each test, just collect them here
set(test_include_dirs ${third_party_include_dirs})
list(APPEND test_include_dirs 	${PROJECT_SOURCE_DIR}/include
								${PROJECT_SOURCE_DIR}/third/catch
								${PROJECT_SOURCE_DIR}/test)

# Creates list of all subdirectories ending in _test
macro(make_subdir_list result curdir)
	file(GLOB children RELATIVE ${curdir} ${curdir}/*_test)
	set(dirlist "")
	foreach(child ${children})
		if(IS_DIRECTORY ${curdir}/${child})
			LIST(APPEND dirlist ${child})
		endif()
	endforeach()
	set(${result} ${dirlist})
endmacro()

make_subdir_list(subdirs ${CMAKE_CURRENT_SOURCE_DIR})
foreach(subdir ${subdirs})
	add_subdirectory(${subdir})
endforeach()
