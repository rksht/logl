#include "duk_helper.h"
#include <learnogl/kitchen_sink.h>
#include <learnogl/start.h>

static void push_file_as_string(duk_context *duk, const fs::path &file_path)
{
	fo::Array<char> text;
	read_file(file_path, text, true);
	duk_push_string(duk, fo::data(text));
}

// A test directory containing js modules
std::vector<fs::path> js_module_paths = { ::make_path(SOURCE_DIR, "js_modules", "root_1"),
					  ::make_path(SOURCE_DIR, "js_modules", "root_2") };

int main()
{
	eng::init_memory();
	DEFERSTAT(eng::shutdown_memory());

	WrappedDukContext dc;

	for (const_ &path : js_module_paths)
	{
		dc.add_module_path(path);
	}

	dc.init();

	dc.exec_file(make_path(SOURCE_DIR, "using_modules.js"));
}
