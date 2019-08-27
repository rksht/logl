// Extruded antialiased lines from OpenGL Insights. Plan is - first do the standard geometry shader version as
// described in the text. Then try to use only vertex shader based approach using storage buffers and
// attributeless rendering. In this I will also incorporate ImGui.

#include <learnogl/imgui_glfw.h>
#include <learnogl/eng>
#include <learnogl/nf_simple.h>

#define TTF_FILE_PATH LOGL_UI_FONT

using namespace eng;
using namespace eng::math;

struct LineVertexData {
	fo::Vector3 position;
	fo::Vector3 normal;
};

struct LineStrip {
	fo::Vector<fo::Vector3> points;

	u32 num_points;

	VertexBufferHandle points_vbo;
	VertexArrayHandle line_strip_format_vao;
};

struct App {
	inistorage::Storage iniconf;
	LineStrip line_strip;
	CameraTransformUB camera_transform_ub;
	ShaderStorageBufferHandle line_strip_ssbo;

	VertexShaderHandle line_vs;
	FragmentShaderHandle debug_line_fs;
	GeometryShaderHandle extrude_line_gs;

	BlendFunctionStateID line_draw_blendstate;
	DepthStencilStateID line_draw_ds_state;
	RasterizerStateID line_draw_rs_state;
};

void init_line_points(App &app)
{
	if (!app.iniconf.cd()) {
		app.iniconf.init_from_file(make_path(SOURCE_FILE, "typed_gl_test.ini.sjson"));
	}
	auto cd = app.iniconf.cd();
	nfcd_loc basic_line_strip_loc = simple_get_qualified(cd, "line_strips.basic_line_strip");
	CHECK_EQ_F(nfcd_type(cd, basic_line_strip_loc), NFCD_TYPE_ARRAY);

	app.line_strip.num_points = (u32)nfcd_array_size(cd, basic_line_strip_loc);
	fo::reserve(app.line_strip.points, (u32)num_points);

	for (int i = 0; i < (int)app.line_strip.num_points; ++i) {
		nfcd_loc point_loc = nfcd_array_item(cd, basic_line_strip_loc, i);

		LineVertexData vertex_data;
		vertex_data.position = SimpleParse<fo::Vector3>(cd, point_loc);
		vertex_data.normal = math::unit_y;
		fo::push_back(app.line_strip.points, vertex_data);
	}

	// Create the VBO and VAO
	auto vao_format = VaoFormatDesc::from_attribute_formats(
		{ VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, 0),
			VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, offsetof(LineVertexData, normal)) });

	app.line_strip.line_strip_format_vao = create_vao(g_rm(), vao_format);

	BufferCreateInfo buffer_ci;
	buffer_ci.bytes = sizeof(LineVertexData) * app.line_strip.num_points;
	buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
	buffer_ci.init_data = fo::data(app.line_strip.points);
	buffer_ci.name = "@line_strip_points_vbo";

	app.line_strip.points_vbo = create_vertex_buffer(g_rm(), buffer_ci);
}

namespace app_loop
{

	template <> void init<App>(App &app)
	{
		init_line_points(app);

		ShaderDefines shader_defs;

		// Load shaders
		auto path_to_lines_shader = make_path(SOURCE_DIR, "extruded_lines.glsl");
		app.line_vs = create_vertex_shader(g_rm(), path_to_lines_shader);
		app.debug_line_fs = create_fragment_shader(g_rm(), path_to_lines_shader);
		app.extruded_line_gs = create_geometry_shader(g_rm(), path_to_lines_shader);

		// Create required render states
		BlendFunctionDesc blend_func_desc;
		blend_func_desc.set_default();
		blend_func_desc.blend_op = BlendOp::FUNC_ADD; // <--- Use MAX as blendop?

		app.line_draw_blendstate = create_blendfunc_state(g_rm(), blend_func_desc);

		RasterizerStateDesc rs_desc;
		rs_desc.set_default();
		rs_desc.cull_side = GL_NONE;

		app.line_draw_rs_state = create_rasterizer_state(g_rm(), rs_desc);

		DepthStencilStateDesc ds_desc;
		ds_desc.set_default();

		app.line_draw_ds_state = create_depth_stencil_state(g_rm(), ds_desc);
	}

	template <> void update<App>(App &a, State &loop_timer) {
		glfwPollEvents();
	}

	template <> void render<App>(App &a) {

		glfwSwapBuffers(gl().window);
	}

	template <> void should_close<App>(App &a) { return false; }

	template <> void close<App>(App &a) {}
} // namespace app_loop

int main(int ac, char **av) {}
