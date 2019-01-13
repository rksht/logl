cmake_minimum_required(VERSION 3.4)

include_directories(${PROJECT_SOURCE_DIR}/third/glad_compat/include)

add_library(glad_compat
                ${PROJECT_SOURCE_DIR}/third/glad_compat/include/glad_compat/glad.h
                ${PROJECT_SOURCE_DIR}/third/glad_compat/src/glad_compat.c)

if (BUILD_SHARED_LIBS)
	target_compile_definitions(glad_compat PRIVATE GLAD_GLAPI_EXPORT GLAD_GLAPI_EXPORT_BUILD INTERFACE GLAD_GLAPI_EXPORT)
endif()
