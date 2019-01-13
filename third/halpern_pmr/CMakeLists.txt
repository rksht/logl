cmake_minimum_required(VERSION 3.4)

include_directories(include)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)

add_library(halpern_pmr
				include/halpern_pmr/polymorphic_allocator.h
                include/halpern_pmr/pmr_string.h
                include/halpern_pmr/pmr_vector.h
                include/halpern_pmr/pmr_map.h
                src/polymorphic_allocator.cpp)

# target_compile_definitions(halpern_pmr PRIVATE PMR_EXPORT PMR_EXPORT_BUILD INTERFACE PMR_EXPORT)
