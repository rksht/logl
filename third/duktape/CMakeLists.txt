cmake_minimum_required(VERSION 3.4)

project(duktape)

set(_duktape_source_files duk_config.h duktape.h duktape.c duk_console.h duk_console.c duk_module_duktape.h duk_module_duktape.c)

ex_prepend_to_each("${_duktape_source_files}" "${CMAKE_CURRENT_SOURCE_DIR}/" DUKTAPE_SOURCE_FILES)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)
add_library(duktape_lib ${DUKTAPE_SOURCE_FILES})
