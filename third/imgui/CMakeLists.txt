cmake_minimum_required(VERSION 3.4)

set(imgui_sources imgui.cpp
				  imgui.h
				  imgui_demo.cpp
				  imgui_draw.cpp
				  imgui_internal.h
				  imgui_widgets.cpp
				  imconfig.h
				  imstb_rectpack.h
				  imstb_textedit.h
				  imstb_truetype.h)

# set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)

if (BUILD_SHARED_LIBS)
	add_library(imgui SHARED ${imgui_sources})

	if (MSVC)
		target_compile_options(imgui BEFORE
			PRIVATE "-DIMGUI_API=__declspec(dllexport)"
			INTERFACE "-DIMGUI_API=__declspec(dllimport)")
	endif()

else()
	add_library(imgui ${imgui_sources})
endif()

add_executable(imgui_demo imgui_demo.cpp)
target_link_libraries(imgui_demo imgui)
