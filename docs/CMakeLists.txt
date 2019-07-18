find_package(Doxygen REQUIRED)

get_target_property(LOGL_HEADER_DIR learnogl INTERFACE_INCLUDE_DIRECTORIES)
file(GLOB_RECURSE LOGL_PUBLIC_HEADERS ${LOGL_HEADER_DIR}/*.h)

# Placeholders in Doxyfile.in
set(DOXYGEN_INPUT_DIR ${PROJECT_SOURCE_DIR}/include)
set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen)

set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)

set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

#Replace variables inside @@ with the current values
configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
                   DEPENDS ${LOGL_PUBLIC_HEADERS}
                   COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                   MAIN_DEPENDENCY Doxyfile
                   COMMENT "Generating docs")

add_custom_target(doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})
