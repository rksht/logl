// Testing skinned mesh loading and playing skeletal animation

#include <learnogl/gl_misc.h>

using namespace eng;

const auto monkey_with_bones_dae_file = make_path(RESOURCES_DIR, "monkey_with_bones.dae");
const auto monkey_obj_file = make_path(RESOURCES_DIR, "suzanne.obj");

const auto glparams = []() {
	StartGLParams p;
	p.window_title = "Bones load test";
	return p;
}();

struct App {
};

void test_mesh_load()
{
	using namespace mesh;
	::mesh::Model monkey_with_bones;
	::mesh::load(monkey_with_bones, monkey_with_bones_dae_file.u8string().c_str());
	CHECK_EQ_F(fo::size(monkey_with_bones._mesh_array), 1);

	
}

int main(int ac, char **av)
{
	init_memory();
	start_gl(glparams);

	DEFERSTAT(shutdown_memory());
}
