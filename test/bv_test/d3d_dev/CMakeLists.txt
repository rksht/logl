cmake_minimum_required(VERSION 3.4)

add_compile_options("-DSOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"" "-DDXERR_LOGURU")

find_package(D3D11)

message("D3D11_LIBRARIES = ${D3D11_LIBRARIES}, D3D_COMPILER_LIB = ${D3D_COMPILER_LIB}")

set(dx_helper_code d3d11_misc.cpp d3d11_misc.h dxerr.h dxerr.cpp d3d_eye.cpp d3d_eye.h d3d_renderdoc.cpp)
if (MSVC)
  add_executable(d3d_test d3d_test.cpp ${dx_helper_code})
  target_link_libraries(d3d_test ${D3D11_LIBRARIES} ${D3D_COMPILER_LIB} ${DXGUID_LIB} learnogl)
  in_tests_folder(d3d_test)

  add_executable(d3d_ray d3d_ray.cpp ${dx_helper_code})
  target_link_libraries(d3d_ray ${D3D11_LIBRARIES} ${D3D_COMPILER_LIB} ${DXGUID_LIB} learnogl)
  in_tests_folder(d3d_ray)

  add_executable(d3d_ssao d3d_ssao.cpp ${dx_helper_code})
  target_link_libraries(d3d_ssao ${D3D11_LIBRARIES} ${D3D_COMPILER_LIB} ${DXGUID_LIB} learnogl)
  in_tests_folder(d3d_ssao)

  add_executable(d3d_skelanim d3d_skelanim.cpp ${dx_helper_code})
  target_link_libraries(d3d_skelanim ${D3D11_LIBRARIES} ${D3D_COMPILER_LIB} ${DXGUID_LIB} learnogl)
  in_tests_folder(d3d_skelanim)

  add_executable(dxmath_vs_logl dxmath_vs_logl.cpp ${dx_helper_code})
  target_link_libraries(dxmath_vs_logl ${D3D11_LIBRARIES} ${D3D_COMPILER_LIB} ${DXGUID_LIB} learnogl)
  in_tests_folder(dxmath_vs_logl)

endif()

add_executable(gen_obj_mesh gen_obj_mesh.cpp rjm_mc.h)
target_link_libraries(gen_obj_mesh learnogl)

