#include <learnogl/app_loop.h>
#include <learnogl/gl_misc.h>

using namespace eng::math;

struct ViewProjEtc
{
	fo::Matrix4x4 view_from_world;
	fo::Matrix4x4 clip_from_view;
	fo::Matrix4x4 view_from_clip; // Inverse of perspective projection matrix
	fo::Matrix4x4 model_to_world; // Keep model to world in this buffer.
	fo::Vector4 eye_position;
	fo::Vector2 screen_wh;
};

struct AdditionalInfoUB
{
	fo::Matrix4x4 x_world_from_local;
	alignas(16) fo::Vector2 viewport_wh;
};

static eng::RasterizerStateDesc rast_state_desc = eng::default_rasterizer_state_desc;
static eng::DepthStencilStateDesc depth_stencil_state_desc = eng::default_depth_stencil_desc;
static eng::BlendFunctionDesc blendfunc_desc = eng::default_blendfunc_state;

static eng::StartGLParams glparams = {};

inistorage::Storage ini;

struct App
{
	// Set this to specify the range of 3D positions over which we generate the data set, basically an
	// AABB
	f32 box_domain_start = -1.0;
	f32 box_domain_end = 1.0;
	u32 texture_resolution = 64;

	eng::VertexBufferHandle domain_box_vbo;
	eng::IndexBufferHandle domain_box_ebo;
	eng::VertexArrayHandle pos3d_vao;

	u32 cube_num_indices;
	GLuint view_proj_ubo;
	GLuint prog_single_pass;
	GLuint volume_sampler_unit;
	GLuint points_vbo;
	u32 num_points;

	eng::Camera camera;
	ViewProjEtc view_proj_etc;

	eng::CameraTransformUB camera_ub;

	eng::UniformBufferHandle camera_ubo_handle;
	eng::Texture3DHandle scalar_texture;

	eng::SamplerObjectHandle volume_sampler;

	bool should_close_app = false;

	void read_params_from_sjson();

	void make_domain_box_vbo();

	void make_point_distribution_vbo();

	void make_3d_texture();
};

void App::read_params_from_sjson()
{
	var_ ini_store = inistorage::Storage(make_path(SOURCE_DIR, "raycast_2.sjson"));

	INI_STORE_DEFAULT(
	  "box_domain_start", ini_store.number, self_.box_domain_start, self_.box_domain_start);
	INI_STORE_DEFAULT("box_domain_end", ini_store.number, self_.box_domain_end, self_.box_domain_end);
	INI_STORE_DEFAULT(
	  "texture_resolution", ini_store.number, self_.box_domain_end, self_.texture_resolution);
}

// A 3d texture denoting a level set that takes the shape of a torus.
void App::make_3d_texture()
{
	const u32 resolution = self_.texture_resolution;
	const u32 num_texels = resolution * resolution * resolution;
	fo::Array<u8> values; // Boolean values really. Inside or outside.
	fo::resize(values, num_texels);

	f32 domain_units_per_texel = (self_.box_domain_end - self_.box_domain_start) / resolution;

	// Torus definition
	var_ torus_R = 3.0f;
	var_ torus_r = 2.0f;

	INI_STORE_DEFAULT("torus_R", ini.number, torus_R, torus_R);

	const_ square_R = torus_R * torus_R;
	const_ square_r = torus_r * torus_r;

	const_ sgndist_torus = lam_(Vec3 point)
	{
		var_ lhs = eng::math::square_magnitude(point) + square_R - square_r;
		lhs = lhs * lhs;
		const_ rhs = 4 * square_R * (point.x * point.x + point.y * point.y);
		const_ f = lhs - rhs;
		return f;
	};

	for (u32 k = 0; k < resolution; ++k)
	{
		for (u32 j = 0; j < resolution; ++j)
		{
			for (u32 i = 0; i < resolution; ++i)
			{
				f32 x = self_.box_domain_start + domain_units_per_texel * ((f32)i + 0.5f);
				f32 y = self_.box_domain_start + domain_units_per_texel * ((f32)j + 0.5f);
				f32 z = self_.box_domain_start + domain_units_per_texel * ((f32)k + 0.5f);

				const u32 texel = k * (resolution * resolution) + j * resolution + i;

				f32 d = sgndist_torus(Vec3(x, y, z));

                if (d <= 0.0f) {
                    d = std::abs(d) / torus_r;
                }

				// Putting that very value into the texel
				values[texel] = d;
			}
		}
	}

	LOG_F(INFO, "....Generated 3d texture");

	eng::TextureCreateInfo texture_ci;
	texture_ci.width = resolution;
	texture_ci.height = resolution;
	texture_ci.depth = resolution;
	texture_ci.texel_info.internal_type = eng::TexelBaseType::E::FLOAT;
	texture_ci.texel_info.interpret_type = eng::TexelInterpretType::E::UNNORMALIZED;
	texture_ci.texel_info.components = eng::TexelComponents::E::R;

	self_.scalar_texture = eng::create_texture_3d(eng::g_rm(), texture_ci, "tex3d@scalar_texture");

	LOG_F(INFO, "....Created GL texture object");

	// Make the sampler object
	eng::SamplerDesc sampler_desc = eng::default_sampler_desc;
	sampler_desc.mag_filter = GL_LINEAR;
	sampler_desc.min_filter = GL_LINEAR_MIPMAP_LINEAR;
	sampler_desc.addrmode_u = sampler_desc.addrmode_v = sampler_desc.addrmode_w = GL_CLAMP_TO_EDGE;

	self_.volume_sampler = eng::create_sampler_object(eng::g_rm(), sampler_desc);
}

void App::make_domain_box_vbo()
{
	eng::BufferCreateInfo buffer_ci = {};

	var_ scale = xyz_scale_matrix(((self_.box_domain_end - self_.box_domain_start) * 0.5f) * one_3);

	eng::mesh::Model model;
	eng::load_cube_mesh(model, scale, false, false);

	// TODO: transform should scale the cube into half-radius (domain_end - box_domain_start) / 2. But
	// identity_matrix corresponds using box_domain_start = -1 and domain_end = 1.

	const auto &m = model[0];

	buffer_ci.bytes = m.o.get_vertices_size_in_bytes();
	buffer_ci.init_data = m.buffer;
	buffer_ci.flags = eng::BufferCreateBitflags::SET_STATIC_STORAGE;
	buffer_ci.name = "vbo@domain_box";

	self_.domain_box_vbo = eng::create_vertex_buffer(eng::g_rm(), buffer_ci);

	buffer_ci = {};
	buffer_ci.bytes = m.o.get_indices_size_in_bytes();
	buffer_ci.init_data = m.buffer + m.o.get_indices_byte_offset();
	buffer_ci.name = "ebo@domain_box";

	self_.domain_box_ebo = eng::create_element_array_buffer(eng::g_rm(), buffer_ci);

	self_.domain_box_vao = eng::g_rm().pos_vao;

	app.domain_box_vbo = vbo;
	app.domain_box_ebo = ebo;
	app.pos3d_vao = pos3d_vao;
	app.cube_num_indices = m.o.num_faces * 3;

	eng::set_vao_label(app.pos3d_vao, "vao@pos_3d");
	eng::set_buffer_label(app.domain_box_vbo, "vbo@cube");
	eng::set_buffer_label(app.domain_box_ebo, "ebo@cube");
}

void App::make_point_distribution_vbo()
{
	const u32 texture_resolution = self_.texture_resolution;
	fo::Array<fo::Vector3> point_values;
	fo::resize(point_values, texture_resolution * texture_resolution * texture_resolution);

	// Cell length in units of domain size
	f32 cell_length = (self_.domain_end - self_.box_domain_start) / f32(texture_resolution);

	// Starting position of domain (in local space only).
	const fo::Vector3 box_domain_start_position = { self_.box_domain_start,
							self_.box_domain_start,
							self_.box_domain_start };

	for (f32 k = 0; k < texture_resolution; ++k)
	{
		for (f32 j = 0; j < texture_resolution; ++j)
		{
			for (f32 i = 0; i < texture_resolution; ++i)
			{
				const u32 texel = (u32)(k * (texture_resolution * texture_resolution) +
							j * texture_resolution + i);

				point_values[texel] = box_domain_start_position +
				  fo::Vector3{ (i)*cell_length, (j)*cell_length, (k)*cell_length };
			}
		}
	}

	LOG_F(INFO, "...Done generating point grid");

	// Make vbo

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vec_bytes(point_values), fo::data(point_values), GL_STATIC_DRAW);

	self_.points_vbo = vbo;
	self_.num_points = fo::size(point_values);
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
	if (sampler_loc != -1)
	{
		glGetUniformiv(program, sampler_loc, &texture_unit);
		app.volume_sampler_unit = texture_unit;
		LOG_F(INFO, "... Volume texture will be bound to unit %i", app.volume_sampler_unit);
	}
	else
	{
		app.volume_sampler_unit = 0;
	}
}

namespace app_loop
{

	template <> void init<App>(App &app)
	{
		app.read_params_from_sjson();

		app.camera.set_eye(eng::eye::toward_negz(1.0f));
		app.camera.update_view_transform();
		app.camera.set_proj(0.2f,
				    1000.0f,
				    70.0 * one_deg_in_rad,
				    glparams.window_height / float(glparams.window_width));

		make_domain_box_vbo(app);

		app.make_3d_texture();
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
		// glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.domain_box_ebo);
		// glBindVertexBuffer(0, app.domain_box_vbo, 0, sizeof(fo::Vector3));
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
