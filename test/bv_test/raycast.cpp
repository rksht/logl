#include <learnogl/app_loop.h>
#include <learnogl/eng>
#include <learnogl/shader.h>

using namespace eng::math;

struct ViewProjEtc {
	fo::Matrix4x4 view_from_world;
	fo::Matrix4x4 clip_from_view;
	fo::Matrix4x4 view_from_clip; // Inverse of perspective projection matrix
	fo::Matrix4x4 model_to_world; // Keep model to world in this buffer.
	fo::Vector4 eye_position;
	fo::Vector2 screen_wh;
};

static eng::RasterizerStateDesc rast_state_desc = eng::default_rasterizer_state_desc;
static eng::DepthStencilStateDesc depth_stencil_state_desc = eng::default_depth_stencil_desc;
static eng::BlendFunctionDesc blendfunc_desc = eng::default_blendfunc_state;

static eng::StartGLParams glparams = {};

struct App {
	// Set this to specify the range of 3D positions over which we generate the data set, basically an AABB
	f32 domain_start = -1.0;
	f32 domain_end = 1.0;
	u32 texture_resolution = 64;
	u32 point_grid_resolution = 64;

	GLuint volume_texture;
	GLuint volume_sampler;
	GLuint cube_vbo, cube_ebo, pos3d_vao;
	u32 cube_num_indices;
	GLuint view_proj_ubo;
	GLuint prog_single_pass;
	GLuint volume_sampler_unit;
	GLuint points_vbo;
	u32 num_points;

	eng::Camera camera;
	ViewProjEtc view_proj_etc;

	bool should_close_app = false;
};

// A 3d texture denoting.. values defined over a sphere volume. resolution is the number of texels in the x, y
// and z direction, so our texture is a cube. Should be simple enough.
void make_3d_texture(App &app)
{
	const u32 resolution = app.texture_resolution;
	const u32 num_texels = resolution * resolution * resolution;
	fo::Array<f32> values;
	fo::resize(values, num_texels);

	f32 domain_units_per_texel = (app.domain_end - app.domain_start) / resolution;

	for (u32 k = 0; k < resolution; ++k) {
		for (u32 j = 0; j < resolution; ++j) {
			for (u32 i = 0; i < resolution; ++i) {
				f32 x = app.domain_start + domain_units_per_texel * ((f32)i + 0.5f);
				f32 y = app.domain_start + domain_units_per_texel * ((f32)j + 0.5f);
				f32 z = app.domain_start + domain_units_per_texel * ((f32)k + 0.5f);

				const u32 texel = k * (resolution * resolution) + j * resolution + i;

				// Distance from circle at origin
				// f32 d = magnitude({ x, y, z });
				f32 d = (f32)rng::random(0.0, 0.5) * magnitude({ x, y, z });

				// Putting that very value into the texel
				values[texel] = d;
			}
		}
	}

	LOG_F(INFO, "....Generated 3d texture");

	GLuint tex;
	glCreateTextures(GL_TEXTURE_3D, 1, &tex);
	glTextureStorage3D(tex, 1, GL_R32F, resolution, resolution, resolution);
	glTextureSubImage3D(
	  tex, 0, 0, 0, 0, resolution, resolution, resolution, GL_RED, GL_FLOAT, fo::data(values));

	eng::set_texture_label(tex, "tex3d@volume_texture");

	LOG_F(INFO, "....Created GL texture object");

	app.volume_texture = tex;

	// Make the sampler object
	eng::SamplerDesc sampler_desc = eng::default_sampler_desc;
	sampler_desc.mag_filter = GL_LINEAR;
	sampler_desc.min_filter = GL_LINEAR_MIPMAP_LINEAR;
	sampler_desc.addrmode_u = sampler_desc.addrmode_v = sampler_desc.addrmode_w = GL_CLAMP_TO_EDGE;
	app.volume_sampler = eng::make_sampler_object(sampler_desc);
}

void make_unit_cube_vbo(App &app)
{
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	eng::mesh::Model model;
	eng::load_cube_mesh(model, identity_matrix, false, false);
	// TODO: transform should scale the cube into half-radius (domain_end - domain_start) / 2. But
	// identity_matrix corresponds using domain_start = -1 and domain_end = 1.

	const auto &m = model[0];
	const u32 vbo_size = m.o.get_vertices_size_in_bytes();
	const u32 ebo_size = m.o.get_indices_size_in_bytes();

	glBufferData(GL_ARRAY_BUFFER, vbo_size, m.buffer, GL_STATIC_DRAW);

	GLuint ebo;
	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo_size, m.buffer + m.o.get_indices_byte_offset(), GL_STATIC_DRAW);

	GLuint pos3d_vao;

	glGenVertexArrays(1, &pos3d_vao);
	glBindVertexArray(pos3d_vao);
	glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, 0);
	glEnableVertexAttribArray(0);

	app.cube_vbo = vbo;
	app.cube_ebo = ebo;
	app.pos3d_vao = pos3d_vao;
	app.cube_num_indices = m.o.num_faces * 3;

	eng::set_vao_label(app.pos3d_vao, "vao@pos_3d");
	eng::set_buffer_label(app.cube_vbo, "vbo@cube");
	eng::set_buffer_label(app.cube_ebo, "ebo@cube");
}

void make_point_distribution_vbo(App &app)
{
	const u32 point_grid_resolution = app.point_grid_resolution;
	fo::Array<fo::Vector3> point_values;
	fo::resize(point_values, point_grid_resolution * point_grid_resolution * point_grid_resolution);

	// Cell length in units of domain size
	f32 cell_length = (app.domain_end - app.domain_start) / f32(point_grid_resolution);

	// Starting position of domain (in local space only).
	const fo::Vector3 domain_start_position = { app.domain_start, app.domain_start, app.domain_start };

	for (f32 k = 0; k < point_grid_resolution; ++k) {
		for (f32 j = 0; j < point_grid_resolution; ++j) {
			for (f32 i = 0; i < point_grid_resolution; ++i) {
				const u32 texel =
				  (u32)(k * (point_grid_resolution * point_grid_resolution) + j * point_grid_resolution + i);

				point_values[texel] =
				  domain_start_position + fo::Vector3{ (i)*cell_length, (j)*cell_length, (k)*cell_length };
			}
		}
	}

	LOG_F(INFO, "...Done generating point grid");

	// Make vbo

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vec_bytes(point_values), fo::data(point_values), GL_STATIC_DRAW);

	app.points_vbo = vbo;
	app.num_points = fo::size(point_values);
}

void make_uniform_buffers(App &app)
{
	GLuint ubo;
	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(ViewProjEtc), nullptr, GL_DYNAMIC_DRAW);
	app.view_proj_ubo = ubo;
	eng::set_buffer_label(ubo, "ubo@view_proj_etc");
}

void load_programs(App &app)
{
	eng::ShaderDefines shader_defs;
	shader_defs.add("VOLUME_TEX_RESOLUTION", (int)app.texture_resolution);

	const auto shader_file = make_path(SOURCE_DIR, "raycast.vsfs.glsl");

	GLuint vs = eng::create_vsfs_shader_object(
	  shader_file, eng::ShaderKind::VERTEX_SHADER, shader_defs, "vs@raycast.vsfs");

	GLuint fs = eng::create_vsfs_shader_object(
	  shader_file, eng::ShaderKind::FRAGMENT_SHADER, shader_defs, "fs@raycast.vsfs");

	GLuint program = eng::create_program(vs, fs, "prog@raycast");
	app.prog_single_pass = program;

	// Sampler binding
	int texture_unit = 0;
	const auto sampler_loc = glGetUniformLocation(program, "volume_sampler");
	if (sampler_loc != -1) {
		glGetUniformiv(program, sampler_loc, &texture_unit);
		app.volume_sampler_unit = texture_unit;
		LOG_F(INFO, "... Volume texture will be bound to unit %i", app.volume_sampler_unit);
	} else {
		app.volume_sampler_unit = 0;
	}
}

namespace app_loop
{

	template <> void init<App>(App &app)
	{
		app.camera.set_eye(eng::eye::toward_negz(1.0f));
		app.camera.update_view_transform();
		app.camera.set_proj(
		  0.2f, 1000.0f, 70.0 * one_deg_in_rad, glparams.window_height / float(glparams.window_width));

		make_unit_cube_vbo(app);
		make_3d_texture(app);
		make_uniform_buffers(app);
		make_point_distribution_vbo(app);

		load_programs(app);

		// Initial transforms.
		app.view_proj_etc.model_to_world = translate(identity_matrix, -unit_z * 1.5f);
		app.view_proj_etc.clip_from_view = app.camera.proj_xform();
		app.view_proj_etc.view_from_world = app.camera.view_xform();
		app.view_proj_etc.view_from_clip = inverse(app.view_proj_etc.clip_from_view);
		app.view_proj_etc.eye_position = fo::Vector4(app.camera.position(), 1.0);

		app.view_proj_etc.screen_wh = { f32(glparams.window_width), f32(glparams.window_height) };

		// initialize render states. default suffices.
		rast_state_desc.cull_side = GL_BACK;

		LOG_F(INFO, "... Done intializing");
	}

	template <> void update<App>(App &app, app_loop::State &timer)
	{
		glfwPollEvents();
		eng::handle_eye_input(
		  eng::gl().window, app.camera._eye, timer.frame_time_in_sec, app.camera._view_xform);

		app.view_proj_etc.view_from_world = app.camera.view_xform();
	}

	template <> void render<App>(App &app)
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		eng::set_gl_rasterizer_state(rast_state_desc);
		// eng::set_gl_blendfunc_state(blendfunc_desc);
		eng::set_gl_depth_stencil_state(depth_stencil_state_desc);

		glUseProgram(app.prog_single_pass);

		glBindTextureUnit(app.volume_sampler_unit, app.volume_texture);
		glBindSampler(app.volume_sampler_unit, app.volume_sampler);

		glInvalidateBufferData(app.view_proj_ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, app.view_proj_ubo);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ViewProjEtc), (void *)&app.view_proj_etc);
		glBindBufferRange(GL_UNIFORM_BUFFER, 0, app.view_proj_ubo, 0, sizeof(ViewProjEtc));

		glBindVertexArray(app.pos3d_vao);
		// glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.cube_ebo);
		// glBindVertexBuffer(0, app.cube_vbo, 0, sizeof(fo::Vector3));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindVertexBuffer(0, app.points_vbo, 0, sizeof(fo::Vector3));

		// glDrawElements(GL_TRIANGLES, app.cube_num_indices, GL_UNSIGNED_SHORT, (const void *)0);
		glDrawArrays(GL_POINTS, 0, app.num_points);
		glfwSwapBuffers(eng::gl().window);
	}

	template <> bool should_close<App>(App &app)
	{
		return glfwWindowShouldClose(eng::gl().window) || app.should_close_app;
	}

	template <> void close<App>(App &app) { eng::close_gl(glparams); }

} // namespace app_loop

int main()
{
	eng::init_memory();
	DEFERSTAT(eng::shutdown_memory());
	glparams.msaa_samples = 1;
	glparams.window_width = 800;
	glparams.window_height = 800;
	glparams.window_title = "single pass raycast";
	glparams.clear_color = colors::PowderBlue;
	eng::start_gl(glparams);
	rng::init_rng();

	app_loop::State timer{};

	App app;

	app_loop::run(app, timer);
}
