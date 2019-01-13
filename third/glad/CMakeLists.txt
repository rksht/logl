cmake_minimum_required(VERSION 3.4)

include_directories(${PROJECT_SOURCE_DIR}/third/glad/include)

add_library(glad
                ${PROJECT_SOURCE_DIR}/third/glad/include/glad/glad.h
                ${PROJECT_SOURCE_DIR}/third/glad/src/glad.c)

if (BUILD_SHARED_LIBS)
	target_compile_definitions(glad PRIVATE GLAD_GLAPI_EXPORT GLAD_GLAPI_EXPORT_BUILD INTERFACE GLAD_GLAPI_EXPORT)
endif()
