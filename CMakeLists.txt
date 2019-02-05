cmake_minimum_required(VERSION 3.4)

SET(MSVC_INCREMENTAL_DEFAULT OFF)

project(logl)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
include(extra_functions)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

ex_set_gcc_or_clang()

option(LOGL_COMPILE_WITH_MT "Compile with /MT or /MTd in MSVC" ON)

if (LOGL_COMPILE_WITH_MT)
  ex_compile_with_mt()
endif()

option(LOGL_ALL_IN_BIN_DIR "Put built output into a 'bin' directory" on)

if ("${LOGL_ALL_IN_BIN_DIR}" OR "${MSVC}")
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# Assimp is fairly heavy, and takes a while to compile. Doesn't depend on
# options either, so putting it here at the top to prevent needless
# recompilation.
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable HLSL support from glslang")
add_subdirectory(third/assimp-4.1.0)

# So does Box2D.
# add_subdirectory(third/liquidfun/liquidfun/Box2D)

if (MSVC)
  add_compile_options("-D_ENABLE_EXTENDED_ALIGNED_STORAGE=1")
endif()

if (LOGL_FSANITIZE_MEMORY)
  add_compile_options(-fsanitize=address)
endif()

# Forcing this
# set(BUILD_SHARED_LIBS 1)

add_subdirectory(third/glfw)

# add_compile_options(-DGLAD_API_EXPORT)

add_subdirectory(third/glad)
add_subdirectory(third/glad_compat)
add_subdirectory(third/halpern_pmr)

if(${LOGL_USE_CPP_17})
  message("LOGL_USE_CPP_17 = ${LOGL_USE_CPP_17}")
  if (gcc_or_clang)
    message("gcc_or_clang = ${gcc_or_clang}")
    add_compile_options(-std=c++17)
  elseif(msvc)
    message("msvc = ${gcc_or_clang}")
    add_compile_options(-std:c++17)
  endif()
endif()

if(LOGL_USE_AVX)
  add_compile_options("-DLOGL_USE_AVX=1")
else()
  add_compile_options("-DLOGL_USE_AVX=0")
endif()

if ({${MSVC})
  add_compile_options(-D_HAS_AUTO_PTR_ETC=1)
endif()

# set(SHADERC_SPIRV_HEADERS_DIR "${PROJECT_SOURCE_DIR}/third/SPIRV-Tools/external/SPIRV-Headers")
# set(SHADERC_SPIRV_TOOLS_DIR "${PROJECT_SOURCE_DIR}/third/SPIRV-Tools")
# set(SHADERC_GLSLANG_DIR "${PROJECT_SOURCE_DIR}/third/glslang")

if (MSVC)
  add_compile_options(-D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
endif()

add_subdirectory(third/fmt)

# Required for shaderc and its deps. They don't use dllexport.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)
set(ENABLE_HLSL OFF CACHE BOOL "Disable HLSL support from glslang")
set(ENABLE_OPT OFF CACHE BOOL "Disable optimizer support from glslang")
set(SHADERC_ENABLE_NV_EXTENSIONS OFF CACHE BOOL "Disabling NV extension for shaderc")
set(ENABLE_NV_EXTENSIONS OFF CACHE BOOL "Disabling NV extension for glslang")
set(SHADERC_SKIP_TESTS ON CACHE BOOL "Disable building test for shaderc")
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "Disable installing shaderc")
add_subdirectory(third/shaderc/src)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS false)

# add_subdirectory(third/duktape)

# Third party include dirs
set(rel_include_dirs
    "glfw/include"
    "scaffold/include"
    "imgui"
    "glad/include"
    "glad_compat/include"
    "argh"
    "clara"
    "assimp-4.1.0/include"
    "fmt/include"
    "spirv-tools/include"
    "shaderc/src/libshaderc/include"
    "shaderc/src/libshaderc_util/include"
    "halpern_pmr/include"
    "mapbox"
    "loguru"
    # "anttweakbar_msvc_cmake/include"
    # "duktape"
    # "liquidfun/liquidfun/Box2D"
)

add_library(loguru third/loguru/loguru.cpp third/loguru/loguru.hpp)

ex_prepend_to_each("${rel_include_dirs}" "${PROJECT_SOURCE_DIR}/third/"
                   third_party_include_dirs)

# message(STATUS "logl third_party_include_dirs = ${third_party_include_dirs}")

include_directories(${third_party_include_dirs})
add_subdirectory(third/scaffold)

# imconfig.h needs to include some of the above
add_subdirectory(third/imgui)

set(LOGL_DATA_DIR "${PROJECT_SOURCE_DIR}/data")
set(LOGL_SHADERS_DIR "${LOGL_DATA_DIR}/shaders")
set(LOGL_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

add_macro_definition(LOGL_DATA_DIR)
add_macro_definition(LOGL_UI_FONT)
add_macro_definition(LOGL_SHADERS_DIR)
add_macro_definition(LOGL_RENDERDOC_DLL_PATH)
add_macro_definition(LOGL_SOURCE_DIR)

if (MSVC)
  add_compile_options(-DNOMINMAX)
endif()

# Testing some D3D11 code
if (MSVC)
  set(WINDOWS_RUNTIME_VERSION "10.0.16299.0" CACHE STRING "Build scaffold as shared lib" FORCE)
  set(WINRT_INCLUDE_DIR "C:/Program Files (x86)/Windows Kits/10/Include/${WINDOWS_RUNTIME_VERSION}/winrt")
  find_package(D3D11)
  find_package(D3D_COMPILER_LIB)
  list(APPEND third_party_include_dirs ${D3D11_INCLUDE_PATH})
  list(APPEND third_party_include_dirs ${WINRT_INCLUDE_DIR})
endif()

option(LOGL_USE_CPP_17 "Build with C++17 flags" off)
option(LOGL_USE_AVX "Use AVX instrinsics" off)
set(LOGL_DATA_DIR "${PROJECT_SOURCE_DIR}/data")
set(LOGL_UI_FONT "${LOGL_DATA_DIR}/RobotoMono-Bold.ttf"
    CACHE STRING "Path to default font"
    FORCE)
set(LOGL_RENDERDOC_DLL_PATH "" CACHE PATH "Path to renderdoc .dll or .so")
option(LOGL_FSANITIZE_MEMORY "Build with -fsanitize=memory" off)

set(LOGL_MSVC_WARNING_LEVEL "2" CACHE STRING "MSVC warning level")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  add_compile_options(-rdynamic)
endif()

# less_msvc_warnings("${LOGL_MSVC_WARNING_LEVEL}")
suppress_all_msvc_warnings()

if (${msvc})
  add_compile_options(-D__PRETTY_FUNCTION__=__FUNCSIG__)
endif()

disable_unwanted_warnings()

add_subdirectory(scripts)
add_subdirectory(src)
add_subdirectory(test)

message("CMAKE_FLAGS = ${CMAKE_CXX_FLAGS}")
