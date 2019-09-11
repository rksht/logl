cmake_minimum_required(VERSION 3.4)

include(extra_functions)

set(CMAKE_VERBOSE_MAKEFILE ON)

if(gcc_or_clang)
  add_compile_options(-Wall -march=native -fmax-errors=1)
else()
  add_compile_options(-Wall)
endif()

# Source files
set(header_dir ${PROJECT_SOURCE_DIR}/include/learnogl)
set(header_files_relative
    debug_break.h
    # audio.h
    app_loop.h
    bitonic_sort.h
    eng
    essential_headers.h
    start.h
    kitchen_sink.h
    pmr_compatible_allocs.h
    uniform_structs.h
    gl_binding_state.h
    constexpr_stuff.h
    shader.h
    random_sampling.h
    callstack.h
    eye.h
    mesh.h
    rng.h
    stb_image.h
    bounding_shapes.h
    intersection_test.h
    nf_simple.h
    cons.h
    nflibs.h
    math_ops.h
    par_shapes.h
    # resource.h
    dds_loader.h
    renderdoc_app.h
    gl_timer_query.h
    colors.h
    vmath.h
    fps.h
    typed_gl_resources.h
    fixed_string_buffer.h
    glsl_inspect.h
    render_utils.h
    string_table.h
    file_monitor.h
    stb_truetype.h
    stb_rect_pack.h
    font.h
    error.h
    input_handler.h
    scene_tree.h)

ex_prepend_to_each("${header_files_relative}" "${header_dir}/" header_paths)

set(source_files
    one_header_libs.cpp
    eye.cpp
    eng
    mesh.cpp
    callstack.cpp
    gl_binding_state.cpp
    shader.cpp
    pmr_compatible_allocs.cpp
    bounding_shapes.cpp
    rng.cpp
    fps.cpp
    gl_timer_query.cpp
    math_ops.cpp
    typed_gl_resources.cpp
    fixed_string_buffer.cpp
    glsl_inspect.cpp
    render_utils.cpp
    par_shapes.cpp
    kitchen_sink.cpp
    nf_simple.cpp
    nf_config_data.cpp
    nf_json_parser.cpp
    nf_memory_tracker.cpp
    nf_string_table.cpp
    dds_loader.cpp
    dds_loader_impl.cpp
    string_table.cpp
    file_monitor.cpp
    font.cpp
    error.cpp
    ${header_paths}
    scene_tree.cpp
    )

include_directories(${third_party_include_dirs})
include_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)

if (NOT ${MSVC})
  add_compile_options(-fmax-errors=1)
endif()

message("BUILD_SHARED_LIBS = ${BUILD_SHARED_LIBS}")

if (BUILD_SHARED_LIBS)
    message("Building shared libs logl")
    add_library(learnogl SHARED ${source_files})
else()
    message("Not building shared libs logl")
    add_library(learnogl ${source_files})
endif()

set(depending_libraries scaffold glad glfw imgui scaffold assimp fmt shaderc halpern_pmr loguru)

if (MSVC)
  list(APPEND depending_libraries DbgHelp.lib dsound.lib)
endif()

if (LOGL_FSANITIZE_MEMORY)
  set(depending_libraries "-lasan;${depending_libraries}")
endif()

if (gcc_or_clang AND NOT APPLE)
    list(APPEND depending_libraries -lstdc++fs -pthread)
elseif (APPLE)
    list(APPEND depending_libraries -lc++fs -pthread)
endif ()

target_link_libraries(learnogl ${depending_libraries})

list(FIND depending_libraries glad _index_of_glad)
list(INSERT depending_libraries ${_index_of_glad} glad_compat)
list(REMOVE_ITEM depending_libraries glad)

message("depending_libraries = ${depending_libraries}")

add_library(learnogl_compat ${source_files})
target_link_libraries(learnogl_compat ${depending_libraries})
