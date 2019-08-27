// #include <glad_compat/glad.h>

#include "b2d_tryout_meshes.inc.h"

#include <learnogl/eng>

#include <learnogl/font.h>
#include <learnogl/nf_simple.h>
#include <learnogl/stopwatch.h>

#include <Box2D/Box2D.h>

// Some hard defines

#define ESTIMATED_ENTITY_COUNT 0

using namespace ::eng;
using namespace ::eng::math;
using fo::IVector2;
using fo::IVector3;
using fo::IVector4;
using fo::Matrix3x3;
using fo::Matrix4x4;
using fo::Vector2;
using fo::Vector3;
using fo::Vector4;

b2Vec2 cast_to_b2vec2(const Vector2 &v) { return b2Vec2(v.x, v.y); }
b2Vec2 cast_to_b2vec2(const Vector3 &v) { return b2Vec2(v.x, v.y); }
Vector2 cast_to_vec2(const b2Vec2 &v) { return Vector2{ v.x, v.y }; }

// ----------------------------- Utilities
fs::path shader_dir = fs::path(G1_DIR) / "shaders";

fs::path in_shader_dir(const char *file) { return shader_dir / file; }

struct BPComponent {
	b2Body *body = nullptr;
};

// ----------------------------- Game Objects

struct GameObject {
	int id;
	Vector3 position;
};

struct Piece {
	GameObject go;
	b2Body *b2_body;
};

struct Box2D_ShapeList {
	b2PolygonShape box_shape;
	b2PolygonShape circle_shape;
};

Box2D_ShapeList g_box2d_shapelist;

struct TriMeshRenderable {
	GLuint vbo = 0;
	GLuint ebo = 0;
	GLuint vao = 0;
};

// Information for a single text billboard
struct GridpointBillboard {
	alignas(16) Vector3 center_pos; // Position in world
	alignas(16) Vector2 extent;			// Rectangle extent
};

// ----------------------------- Shader related structs

struct RenderStates {
	RasterizerStateID rs_grid_overlay;
};

RenderStates g_render_states;

void init_render_states()
{
	
}

struct VsFsProg {
	GLuint vs = 0;
	GLuint fs = 0;
	GLuint prog = 0;
	UniformVariablesMap uniform_map{ g_st() };
};

struct ShaderPrograms {
	VsFsProg debug_grid;
	VsFsProg grid_points;
};

// A struct list of uniform block bindpoints for clarity
struct UniformBlockBindpoints {
	GLuint reserved;
};

struct GLProgAccessBuffers {
	GLuint billboard_data_array;
};

// ------------------------------

struct GameStruct {
	StartGLParams glparams;
	f32 pixels_per_meter;

	Vector2 world_gravity;

	b2World *b2_world;

	fo::Vector<Piece> pieces;

	inistorage::Storage iniconf{ make_path(G1_DIR, "b2d_tryout.ini.sjson") };

	Camera world_camera;

	ShaderPrograms glprogs;
	ShaderDefines common_shader_defs;
	UniformBlockBindpoints uniform_block_bindpoints;
	GLProgAccessBuffers prog_buffers;

	fo::Array<GridpointBillboard> grid_point_billboards;

	bool should_quit = false;

	StaticModelStorage static_model_store{ ESTIMATED_ENTITY_COUNT };

	~GameStruct() { make_delete(fo::memory_globals::default_allocator(), b2_world); }
};

GLOBAL_STORAGE(GameStruct, game_struct_alnstore);
GLOBAL_ACCESSOR_FN(GameStruct, game_struct_alnstore, g_gamestruct);
#define G_GAMESTRUCT g_gamestruct()

struct DebugGridParms {
	struct Tweak {
		Vector2 bounds_x;
		Vector2 bounds_y;
		Vector2 cell_size_xy;
	};
	Tweak tweak;

	Vector2 length_xy;
	IVector2 line_count_xy;
	Vector2 min_vertex_position;
};

static DebugGridParms g_debug_grid_parms;

void init_debug_grid_parms()
{
	auto &g = g_debug_grid_parms;
	g.length_xy = { g.tweak.bounds_x.y - g.tweak.bounds_x.x, g.tweak.bounds_y.y - g.tweak.bounds_y.x };

	i32 line_count_x = (i32)ceil_div(g.length_xy.x, g.tweak.cell_size_xy.x) + 1;
	i32 line_count_y = (i32)ceil_div(g.length_xy.y, g.tweak.cell_size_xy.y) + 1;
	g.line_count_xy = IVector2{ line_count_x, line_count_y };
	g.min_vertex_position = Vector2{ g.tweak.bounds_x.x, g.tweak.bounds_y.x };
}

const f32 BOARD_Z_WRT_CAMERA = -5.0f; // The z position of the Box2D plane (board) wrt to the camera

const OrthoRange render_world_xrange = { -10.5f, 10.5f };
const OrthoRange render_world_yrange = { -10.5f, 10.5f };
const OrthoRange render_world_zrange = { -0.0f, -1000.0f };

void create_render_world_camera()
{
	// XY plane in game world is transformed to XY plane in render world.

	const OrthoRange zrange_wrt_camera = { -0.0f, -1000.0f };

	G_GAMESTRUCT.world_camera.set_orthogonal_axes(zero_3, Vector3{ 0.0f, 0.0f, -BOARD_Z_WRT_CAMERA }, unit_y);

	G_GAMESTRUCT.world_camera.set_ortho_proj(render_world_xrange.reverse(),
																					 { -1.0f, 1.0f },
																					 render_world_yrange,
																					 { -1.0f, 1.0f },
																					 zrange_wrt_camera,
																					 { -1.0f, 1.0f });

	// print_matrix_classic("View xform = ", G_GAMESTRUCT.world_camera.view_xform());
	// print_matrix_classic("Proj xform = ", G_GAMESTRUCT.world_camera.proj_xform());
}

void init_grid_point_billboards()
{
	auto &g = g_debug_grid_parms;

	const Vector2 extent = g.tweak.cell_size_xy * 0.1f;
	fo::reserve(G_GAMESTRUCT.grid_point_billboards, g.line_count_xy.x * g.line_count_xy.y);

	LOG_F(INFO, "Line count xy = [%i, %i]", XY(g.line_count_xy));

	for (int y = 0; y < g.line_count_xy.y; ++y) {
		Vector2 row_start_point = g.min_vertex_position;
		row_start_point.y += (f32)y * g.tweak.cell_size_xy.y;

		for (int x = 0; x < g.line_count_xy.x; ++x) {
			auto &billboard_data = push_back_get(G_GAMESTRUCT.grid_point_billboards, {});

			billboard_data.center_pos = Vector3{ XY(row_start_point), render_world_zrange.min };
			billboard_data.center_pos.x += (f32)x * g.tweak.cell_size_xy.x;

			billboard_data.extent = extent;

			// LOG_F(INFO, "Pos = " VEC3_FMT(3), XYZ(billboard_data.center_pos));
		}
	}

	LOG_F(INFO, "Num billboards = %u", fo::size(G_GAMESTRUCT.grid_point_billboards));

	G_GAMESTRUCT.prog_buffers.billboard_data_array =
		eng::create_uniform_buffer(vec_bytes(G_GAMESTRUCT.grid_point_billboards), GL_DYNAMIC_DRAW);
	glNamedBufferSubData(G_GAMESTRUCT.prog_buffers.billboard_data_array,
											 0,
											 vec_bytes(G_GAMESTRUCT.grid_point_billboards),
											 fo::data(G_GAMESTRUCT.grid_point_billboards));
}

struct CameraUB {
	fo::Matrix4x4 view;
	fo::Matrix4x4 proj;
	fo::Vector4 _unused;
};

void update_camera_ubo()
{
	// static_assert(sizeof(CameraUB) == 2 * sizeof(Matrix4x4), "static_assert");
	CameraUB ub{ G_GAMESTRUCT.world_camera.view_xform(), G_GAMESTRUCT.world_camera.proj_xform() };
	// glInvalidateBufferData(g_bs().per_camera_ubo());
	glNamedBufferSubData(g_bs().per_camera_ubo(), 0, sizeof(ub), &ub);
}

TU_LOCAL void read_props_from_ini()
{
	auto cd = G_GAMESTRUCT.iniconf.cd();
	auto objects_array_loc = simple_get_qualified(cd, "objects");
	CHECK_EQ_F(nfcd_type(cd, objects_array_loc), NFCD_TYPE_ARRAY);

	int num_objects = nfcd_array_size(cd, objects_array_loc);

	LOG_F(INFO, "num_objects = %i", num_objects);

	fo::reserve(G_GAMESTRUCT.pieces, (u32)num_objects);

	for (int i = 0; i < num_objects; ++i) {
		auto object_loc = nfcd_array_item(cd, objects_array_loc, i);
		CHECK_EQ_F(nfcd_type(cd, object_loc), NFCD_TYPE_OBJECT);
	}
}

void init_stuff_from_ini()
{
	auto &iniconf = G_GAMESTRUCT.iniconf;

	// Debug grid

	INI_STORE_DEFAULT("window.width", iniconf.number, G_GAMESTRUCT.glparams.window_width, 640);
	INI_STORE_DEFAULT("window.height", iniconf.number, G_GAMESTRUCT.glparams.window_height, 480);
	INI_STORE_DEFAULT("b2d_world.pixels_per_meter", iniconf.number, G_GAMESTRUCT.pixels_per_meter, 10.0f);
	INI_STORE_DEFAULT("window.clear_color", iniconf.vector4, G_GAMESTRUCT.glparams.clear_color, colors::Black);
	// INI_STORE_DEFAULT("single_ball_radius", iniconf.number, G_GAMESTRUCT.single_ball.radius, 10.0f);

	INI_STORE_DEFAULT(
		"debug_grid.bounds_x", iniconf.vector2, g_debug_grid_parms.tweak.bounds_x, (Vector2{ 100.0f, 100.f }));
	INI_STORE_DEFAULT(
		"debug_grid.bounds_y", iniconf.vector2, g_debug_grid_parms.tweak.bounds_y, (Vector2{ 100.0f, 100.f }));
	INI_STORE_DEFAULT("debug_grid.cell_size_xy",
							iniconf.vector2,
							g_debug_grid_parms.tweak.cell_size_xy,
							(Vector2{ 10.0f, 10.0f }));

#undef INI_STORE_DEFAULT

	init_debug_grid_parms();

	// Initialize the box2d world object
	G_GAMESTRUCT.b2_world = fo::make_new<b2World>(fo::memory_globals::default_allocator(), b2Vec2(0.0f, -9.8f));

	G_GAMESTRUCT.glparams.gl_forward_compat = false;
	G_GAMESTRUCT.glparams.window_title = "b2d tryout";

	eng::start_gl(G_GAMESTRUCT.glparams);

	// Camera transform
	create_render_world_camera();

	G_GAMESTRUCT.uniform_block_bindpoints.reserved = eng::g_bs().reserve_uniform_bindpoint();
	G_GAMESTRUCT.common_shader_defs.add("RESERVED_UBLOCK_BINDPOINT",
																			(int)G_GAMESTRUCT.uniform_block_bindpoints.reserved);

	init_grid_point_billboards();

	// Get the props
	// read_props_from_ini();
}

void init_bodies()
{
	b2BodyDef bodydef;
	bodydef.type = b2_staticBody;
	bodydef.position.Set(0, 0);
	b2PolygonShape box_shape;

	b2FixtureDef fixture_def;

	b2Body *wall_boundary_body = G_GAMESTRUCT.b2_world->CreateBody(&bodydef);

	fixture_def.shape = &box_shape;

	box_shape.SetAsBox(20, 1, b2Vec2(0, 0), 0); // Lower wall
	wall_boundary_body->CreateFixture(&fixture_def);

	box_shape.SetAsBox(20, 1, b2Vec2(0, 40), 0); // Upper wall (i.e. ceiling)
	wall_boundary_body->CreateFixture(&fixture_def);

	box_shape.SetAsBox(1, 20, b2Vec2(-20, 20), 0); // Left wall
	wall_boundary_body->CreateFixture(&fixture_def);

	box_shape.SetAsBox(1, 20, b2Vec2(20, 20), 0); // Right wall
	wall_boundary_body->CreateFixture(&fixture_def);

	printf("Created walls");
}

void load_shader_objects()
{
	G_GAMESTRUCT.common_shader_defs.add("CAMERA_UBO_BINDPOINT", (int)g_bs().per_camera_ubo_binding());

	// Debug line drawing shader.
	{
		auto &vsfs = G_GAMESTRUCT.glprogs.debug_grid;
		vsfs.vs = create_shader_object(
			in_shader_dir("debug_line_vs.vert"), ShaderKind::VERTEX_SHADER, G_GAMESTRUCT.common_shader_defs);

		vsfs.fs = create_shader_object(
			in_shader_dir("debug_line_fs.frag"), ShaderKind::FRAGMENT_SHADER, G_GAMESTRUCT.common_shader_defs);

		vsfs.prog = create_program(vsfs.vs, vsfs.fs, "@debug_line_prog");
		vsfs.uniform_map.set_program(vsfs.prog);

		vsfs.uniform_map.add_variable("cell_size_xy", Vector2{});
		vsfs.uniform_map.add_variable("length_xy", Vector2{});
		vsfs.uniform_map.add_variable("line_count_xy", IVector2{});
		vsfs.uniform_map.add_variable("min_vertex_position", Vector2{});
		vsfs.uniform_map.add_variable("horizontal_or_vertical", i32(0));
	}

	// Grid point drawing shader
	{
		auto &vsfs = G_GAMESTRUCT.glprogs.grid_points;

		G_GAMESTRUCT.common_shader_defs.add("NUM_BILLBOARDS", (int)fo::size(G_GAMESTRUCT.grid_point_billboards));

		vsfs.vs = create_vsfs_shader_object(in_shader_dir("debug_line_points_vsfs.glsl"),
																				ShaderKind::VERTEX_SHADER,
																				G_GAMESTRUCT.common_shader_defs);
		vsfs.fs = create_vsfs_shader_object(in_shader_dir("debug_line_points_vsfs.glsl"),
																				ShaderKind::FRAGMENT_SHADER,
																				G_GAMESTRUCT.common_shader_defs);
		vsfs.prog = create_program(vsfs.vs, vsfs.fs, "@grid_points_prog");
	}
}

void draw_debug_grid_with_points()
{
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glUseProgram(G_GAMESTRUCT.glprogs.debug_grid.prog);
	glBindVertexArray(g_bs().no_attrib_vao());
	glBindVertexBuffer(0, 0, 0, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	auto &vsfsprog = G_GAMESTRUCT.glprogs.debug_grid;
	vsfsprog.uniform_map.set_uniform("length_xy", g_debug_grid_parms.length_xy);
	vsfsprog.uniform_map.set_uniform("cell_size_xy", g_debug_grid_parms.tweak.cell_size_xy);
	vsfsprog.uniform_map.set_uniform("line_count_xy", g_debug_grid_parms.line_count_xy);
	vsfsprog.uniform_map.set_uniform("min_vertex_position", g_debug_grid_parms.min_vertex_position);
	// Draw vertical lines
	vsfsprog.uniform_map.set_uniform("horizontal_or_vertical", 1);
	glDrawArrays(GL_LINES, 0, 2 * g_debug_grid_parms.line_count_xy.x);
	// Draw horizontal lines
	vsfsprog.uniform_map.set_uniform("horizontal_or_vertical", 0);
	glDrawArrays(GL_LINES, 0, 2 * g_debug_grid_parms.line_count_xy.y);

	// Draw grid point billboards.
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBlendEquation(GL_FUNC_ADD);

	glUseProgram(G_GAMESTRUCT.glprogs.grid_points.prog);

	glBindBuffer(GL_UNIFORM_BUFFER, G_GAMESTRUCT.prog_buffers.billboard_data_array);
	glBindBufferRange(GL_UNIFORM_BUFFER,
										G_GAMESTRUCT.uniform_block_bindpoints.reserved,
										G_GAMESTRUCT.prog_buffers.billboard_data_array,
										0,
										vec_bytes(G_GAMESTRUCT.grid_point_billboards));

	const size_t total_points = fo::size(G_GAMESTRUCT.grid_point_billboards) * 6;
	glDrawArrays(GL_TRIANGLES, 0, total_points);
}

struct Loop {

	void init()
	{
		init_stuff_from_ini();

		load_shader_objects();
	}

	void update(f32 round_time)
	{
		glfwPollEvents();

		if (glfwGetKey(eng::gl().window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			G_GAMESTRUCT.should_quit = true;
		}

		if (eng::handle_eye_input(eng::gl().window,
															G_GAMESTRUCT.world_camera._eye,
															round_time,
															G_GAMESTRUCT.world_camera._view_xform)) {
		}

		if (G_GAMESTRUCT.should_quit) {
			close();
		}
	}

	void draw()
	{
		// Clear default fb
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		update_camera_ubo();

		// Render the bodies of the gameworld

		draw_debug_grid_with_points();

		glfwSwapBuffers(eng::gl().window);
	}

	void close()
	{
		G_GAMESTRUCT.~GameStruct();

		eng::close_gl(G_GAMESTRUCT.glparams);
	}

	void run()
	{
		init();

		stop_watch::HighRes sw;
		start(sw);

		while (!glfwWindowShouldClose(eng::gl().window)) {
			f32 round_time = (f32)seconds(stop_watch::restart(sw));
			update(round_time);

			draw();
		}

		close();
	}
};

int main(int ac, char **av)
{
	init_memory();
	DEFERSTAT(shutdown_memory());

	// Create our game global struct
	new (&G_GAMESTRUCT) GameStruct;
	// DEFERSTAT(G_GAMESTRUCT.~GameStruct());

	Loop loop;
	loop.run();
}
