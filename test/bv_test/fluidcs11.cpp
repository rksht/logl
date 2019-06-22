#include <learnogl/essential_headers.h>
#include <learnogl/gl_timer_query.h>
#include <learnogl/nf_simple.h>

#include <learnogl/app_loop.h>
#include <learnogl/gl_misc.h>

#include <learnogl/bitonic_sort.h>
#include <scaffold/ordered_map.h>

#include "imgui_gl3_render.inc.h"
#include "imgui_glfw_input.inc.h"

#include "gaussian_blur_utils.h"

using namespace fo;
using namespace ::eng::math;
using namespace ::eng::gl_desc;

using namespace std::string_literals;

template <typename BufferDesc> struct BufferBinding : public std::pair<BufferDesc, GLuint> {
	// static_assert(one_of_type_v<BufferDesc, UniformBuffer, ShaderStorageBuffer>, "");

	using Base = std::pair<BufferDesc, GLuint>;

	using Base::first;
	using Base::second;

	static constexpr GLenum gl_buffer_type =
	  std::conditional_t<std::is_same_v<BufferDesc, ShaderStorageBuffer>,
						 std::integral_constant<GLenum, GL_SHADER_STORAGE_BUFFER>,
						 std::conditional_t<std::is_same_v<BufferDesc, UniformBuffer>,
											std::integral_constant<GLenum, GL_UNIFORM_BUFFER>,
											std::integral_constant<GLenum, GL_NONE>>>::value;

	GLuint handle() const { return first.handle(); }
	GLuint bindpoint() const { return second; }

	BufferBinding() = default;

	BufferBinding(const std::pair<BufferDesc, GLuint> &pair)
		: CTOR_INIT_FIELD(Base, pair)
	{
	}

	void bind_and_write(u32 bytes, void *data = nullptr, bool invalidate = true)
	{
		if (invalidate) {
			glInvalidateBufferData(handle());
		}
		if (data != nullptr) {
			glNamedBufferSubData(handle(), 0, bytes, data);
		}
	}

	void bind()
	{
		// LOG_F(INFO, "Binding ubo %u to bindpoint %u", handle(), bindpoint());
		glBindBuffer(gl_buffer_type, handle());
		glBindBufferRange(gl_buffer_type, bindpoint(), handle(), first._offset, first._size);
	}
};

fs::path demo_shader_path(const char *shader_file_name)
{
	return make_path(SOURCE_DIR, "data", shader_file_name);
}

GLuint create_shader_from_big_file(const char *shader_file,
								   eng::ShaderKind shader_kind,
								   eng::ShaderDefines &defs,
								   const char *debug_label = nullptr)
{
	GLuint shader = eng::create_shader_object(demo_shader_path(shader_file), shader_kind, defs, debug_label);
	return shader;
};

eng::ComputeShaderAndProg create_compute_shader_program(const char *shader_file,
														eng::ShaderDefines &defs,
														const char *debug_label = nullptr)
{
	GLuint shader = create_shader_from_big_file(shader_file, eng::COMPUTE_SHADER, defs, debug_label);
	GLuint program = eng::create_compute_program(shader);
	eng::set_program_label(program, fmt::format("compute_prog@{}", debug_label).c_str());
	return { shader, program };
};

// Particle state
struct Particle {
	Vector2 position;
	Vector2 velocity;
};

struct ParticleDensity {
	f32 density;
};

struct ParticleForce {
	Vector2 acceleration;
};

constexpr u32 NUM_GRID_CELLS = 65536;
constexpr u32 SIMULATION_BLOCK_SIZE = 256;

// I restrict the max threads per group to D3D11 hardware capabilities
constexpr std::array<u32, 3> MAX_THREADS_PER_GROUP = { 1024, 1024, 64 };

// Number of threads per block for the bitonic sorting
constexpr u32 NUM_ELEMENTS_PER_GROUP = 512;
constexpr u32 TRANSPOSE_BLOCK_SIZE = 16;
constexpr u32 MAX_ELEMENTS_ALLOWED = NUM_ELEMENTS_PER_GROUP * NUM_ELEMENTS_PER_GROUP;

constexpr u32 NUM_PARTICLES_HALFK = 512;
constexpr u32 NUM_PARTICLES_1K = 1024;
constexpr u32 NUM_PARTICLES_8K = 8 * 1024;
constexpr u32 NUM_PARTICLES_16K = 16 * 1024;
constexpr u32 NUM_PARTICLES_32K = 32 * 1024;
constexpr u32 NUM_PARTICLES_64K = 64 * 1024;

constexpr f32 DUMMY_MAP_HEIGHT = 1.0f;
constexpr f32 DUMMY_MAP_WIDTH = 1.0f;

constexpr f32 GRAVITY_ACC = 9.8f;

constexpr u32 SRC_INDEX = 0;
constexpr u32 DEST_INDEX = 1;

inline constexpr Vector2 gravity_down(f32, f32);
inline constexpr Vector2 gravity_up(f32, f32);
inline constexpr Vector2 gravity_left(f32, f32);
inline constexpr Vector2 gravity_right(f32, f32);

enum class SimulationMode : i32 { SIMPLE, SHARED, GRID };

enum WallName { BOTTOM, RIGHT, TOP, LEFT };

inline constexpr u32 num_elements_in_shared_mem(u32 element_size)
{
	constexpr u32 MIN_SHARED_MEM_SIZE = 32u << 10;
	return MIN_SHARED_MEM_SIZE / element_size;
}

constexpr std::pair<f32, f32> PARTICLE_RENDER_SIZE_MIN_MAX = { 0.003f, 0.01f };
constexpr std::pair<f32, f32> VISCOSITY_MIN_MAX = { 0.1f, 0.8f };

// Configuration struct. Initialized with defaults. Some can be changed during startup via the ini file.
struct Config {
	// These will control how the fluid behaves
	f32 initial_particle_spacing = 0.0045f;
	f32 smooth_len = 0.012f;
	f32 pressure_stiffness = 200.0f;
	f32 rest_density = 1000.0f;
	f32 particle_mass = 0.0002f;
	f32 viscosity = 0.1f;
	f32 max_allowable_timestep = 0.005f;

	f32 particle_render_size = 0.003f;
	// f32 particle_render_size = 1.0f;

	// Gravity directions. Gravity is the external force on each particle.
	Vector2 gravity = { 0.0f, -0.05f };

	// The width and height of the world map.
	f32 map_height = 1.2f;
	f32 map_width = (4.0f / 3.0f) * map_height;

	// Map wall collision planes (or rather, lines or edges) in the (N, D) form
	f32 wall_stiffness = 8000.0f;
	std::array<Vector4, 4> planes = {
		Vector4{ 1.f, 0.f, 0.f, 0.f },		  // LEFT
		Vector4{ 0.f, 1.f, 0.f, 0.f },		  // BOTTOM
		Vector4{ -1.f, 0.f, map_width, 0.f }, // RIGHT
		Vector4{ 0.f, -1.f, map_height, 0.f } // TOP
	};

	u32 num_particles = NUM_PARTICLES_1K;

	float particle_size = 0.3f;

	// Stuff related to the Gaussian blur of the 2D density texture performed during rendering
	int gaussian_kernel_size = 9;
};

Config gconf;
constexpr Config default_config = Config{};

// Trying out this segregation of resources as done in the DirectX sample

struct VSFS_Program {
	GLuint vs = 0;
	GLuint fs = 0;
	GLuint program = 0;
};

struct GLResources {
	GLuint null_pbo = 0;

	VSFS_Program record_attribute_prog;
	VSFS_Program blur_only_density_prog;
	VSFS_Program color_density_prog;

	eng::ComputeShaderAndProg build_grid_csprog;
	eng::ComputeShaderAndProg clear_grid_indices_csprog;
	eng::ComputeShaderAndProg build_grid_indices_csprog;
	eng::ComputeShaderAndProg rearrange_particles_csprog;

	// Depending on the algo, we will use the following kernels for density and force
	eng::ComputeShaderAndProg density_simple_csprog;
	eng::ComputeShaderAndProg force_simple_csprog;

	eng::ComputeShaderAndProg density_shared_csprog;
	eng::ComputeShaderAndProg force_shared_csprog;

	eng::ComputeShaderAndProg density_grid_csprog;
	eng::ComputeShaderAndProg force_grid_csprog;

	eng::ComputeShaderAndProg integrate_csprog;

	eng::ComputeShaderAndProg sort_bitonic_csprog;
	eng::ComputeShaderAndProg sort_transpose_csprog;

	eng::ComputeShaderAndProg dbg_advance_csprog;

	BufferBinding<ShaderStorageBuffer> particles_ssbo_binding;
	BufferBinding<ShaderStorageBuffer> sorted_particles_ssbo_binding;
	BufferBinding<ShaderStorageBuffer> densities_ssbo_binding;
	BufferBinding<ShaderStorageBuffer> forces_ssbo_binding;
	BufferBinding<ShaderStorageBuffer> grid_ssbo_binding;

	// Writable bindings for the same buffers as above. @rksht - Only 8 active SSBOs can be bound (minimum,
	// but still) to the compute shader at a time. So consider using the binding state save here. But that's
	// overdoing it, so don't bother right now.
	BufferBinding<ShaderStorageBuffer> forces_rw_ssbo_binding;
	BufferBinding<ShaderStorageBuffer> densities_rw_ssbo_binding;

	BufferBinding<UniformBuffer> sim_constants_ubo_binding;
	BufferBinding<UniformBuffer> camera_ubo;
	BufferBinding<UniformBuffer> blur_params_ubo_binding;

	std::array<eng::BufferDeleter, 2> numbers_ssbos;

	GLuint blur_read_sampler;
};

struct DensityBlurData {
	int kernel_size;
};

struct SimulationConstantsUB {
	u32 num_particles;
	f32 timestep;
	f32 smooth_len;
	f32 pressure_stiffness;
	f32 rest_density;
	f32 density_coef;
	f32 grad_pressure_coef;
	f32 lap_viscosity_coef;
	f32 wall_stiffness;

	Vector4 gravity; // xy only
	Vector4 grid_dim;
	Vector4 planes[4];
	Vector2 screen_wh;

	f32 particle_render_size;
};

eng::StartGLParams glparams;

void init_gl_params()
{
	glparams.msaa_samples = 1;
	glparams.window_title = "SPH in OpenGL";
	glparams.window_width = 1024;
	glparams.window_height = 768;
	glparams.load_renderdoc = false;
	glparams.abort_on_error = true;
	glparams.shader_globals_config.print_after_preprocessing = false;
	glparams.clear_color = colors::LightYellow;
}

BufferBinding<UniformBuffer> create_and_bind_ubo(eng::BindingState &bs,
												 u32 bytes,
												 GLbitfield usage_flags = GL_DYNAMIC_STORAGE_BIT,
												 const char *debug_label = nullptr,
												 void *init_data = nullptr)
{
	GLuint b;
	glCreateBuffers(1, &b);
	eng::set_buffer_label(b, debug_label);
	glNamedBufferStorage(b, bytes, init_data, usage_flags);

	UniformBuffer desc(b, 0, bytes);
	GLuint bindpoint = bs.bind_unique(desc);
	return std::make_pair(desc, bindpoint);
}

template <typename StructureType>
BufferBinding<ShaderStorageBuffer> create_and_bind_ssbo(eng::BindingState &bs,
														GLuint bindpoint,
														u32 count,
														GLbitfield usage_flags,
														const char *debug_label,
														void *init_data = nullptr)
{

	const u32 bytes = count * sizeof(StructureType);

	GLuint b;
	glCreateBuffers(1, &b);
	eng::set_buffer_label(b, debug_label);
	glNamedBufferStorage(b, bytes, init_data, usage_flags);

	ShaderStorageBuffer desc(b, 0, bytes);

	// @rksht - NOT using the BindingState to assign bindings automatically for SSBOs. Assigning them manually
	// for now.
#if 0
    GLuint bindpoint = bs.bind_unique(desc);
#else
	glBindBufferRange(GL_SHADER_STORAGE_BUFFER, bindpoint, b, 0, bytes);
#endif

	LOG_F(INFO,
		  "Creating SSBO %s of size %zu, bound to index %u",
		  debug_label ? debug_label : "<unnamed>",
		  sizeof(StructureType) * count,
		  bindpoint);
	return std::make_pair(desc, bindpoint);
}

template <typename StructureType>
BufferBinding<ShaderStorageBuffer> register_another_binding(eng::BindingState &bs,
															const BufferBinding<StructureType> &binding)
{
	BufferBinding<ShaderStorageBuffer> other_binding(binding);
	other_binding.first._handle = binding.handle();
	other_binding.second = bs.reserve_ssbo_bindpoint();
	return other_binding;
}

// @rksht - When we use automatic bindpoint save/restore, we will not need this manual check. But I don't want
// to use that just yet.
enum SsboBindpoints : u16 {
	PARTICLES_RO = 0,
	PARTICLES_RW = 1,
	FORCES = 2,
	DENSITIES = 3,
};

constexpr u32 MAX_ACTIVE_SSBO_BINDPOINTS = 8;

eng::ShaderDefines g_shader_defs;

TU_LOCAL eng::RasterizerStateDesc rs_desc_particles = eng::default_rasterizer_state_desc;
TU_LOCAL eng::DepthStencilStateDesc ds_desc_particles = eng::default_depth_stencil_desc;
TU_LOCAL eng::BlendFunctionDesc blend_desc_particles = eng::default_blendfunc_state;

struct BlurParamsUB {
	u32 b_horizontal_or_vertical;
};

struct Demo {
	eng::GLApp *gl = nullptr;

	GLResources res;

	SimulationMode cur_simulation_mode = SimulationMode::SIMPLE;

	eng::CameraTransformUB camera_ub_data = {};

	SsboBindpoints ssbo_bindpoints_set;
	GLuint ssbo_bound_at[MAX_ACTIVE_SSBO_BINDPOINTS];

	u32 rast_state_particles;
	u32 ds_state_particles;
	eng::BlendFunctionDesc blend_config_particles;

	bool escape_pressed = false;

	inistorage::Storage ini_conf{ make_path(SOURCE_DIR, "data", "fluid_config.ini") };

	fo::Array<Particle> tmp_init_particles{ fo::memory_globals::default_allocator() };

	eng::FBO screen_space_attrib_fbo;
	eng::FBO density_blur_fbo;

	Config ui_updated_config;

	eng::gl_timer_query::TimerQueryManager timer_manager{ eng::gl().string_table };
	eng::gl_timer_query::TimerID timer_for_simulate;
	eng::gl_timer_query::TimerID timer_for_render;
	eng::gl_timer_query::TimerID timer_for_blit;

	bool show_imgui_debug_window = true;
};

void bind_ssbo(Demo &a, GLuint new_ssbo, GLuint ssbo_bindpoint, u32 ssbo_size)
{
	a.ssbo_bound_at[ssbo_bindpoint] = new_ssbo;
	glBindBufferRange(GL_SHADER_STORAGE_BUFFER, ssbo_bindpoint, new_ssbo, 0, ssbo_size);
}

// Just an overload so I don't have to write .handle() all the time.
void bind_ssbo(Demo &a,
			   BufferBinding<ShaderStorageBuffer> &ssbo_binding,
			   GLuint ssbo_bindpoint,
			   u32 ssbo_size)
{
	a.ssbo_bound_at[ssbo_bindpoint] = ssbo_binding.handle();
	glBindBufferRange(GL_SHADER_STORAGE_BUFFER, ssbo_bindpoint, ssbo_binding.handle(), 0, ssbo_size);
}

void create_render_states(Demo &a)
{
	{
		auto &s = rs_desc_particles;
		s.cull_side = GL_NONE;
		a.rast_state_particles = eng::gl().bs.add_rasterizer_state(s);
	}

	{
		auto &s = ds_desc_particles;
		s.enable_depth_test = GL_FALSE;
		s.depth_compare_func = GL_LEQUAL;
		a.ds_state_particles = eng::gl().bs.add_depth_stencil_state(s);
	}

	{
		auto &s = blend_desc_particles;
		s.src_rgb_factor = eng::BlendFactor::SRC_ALPHA;
		s.dst_rgb_factor = eng::BlendFactor::ONE_MINUS_SRC_ALPHA;
		s.src_alpha_factor = eng::BlendFactor::ONE;
		s.dst_alpha_factor = eng::BlendFactor::ZERO;
		s.blend_op = eng::BlendOp::FUNC_ADD;
		a.blend_config_particles = s;
	}

	{

		eng::SamplerDesc desc = eng::default_sampler_desc;
		desc.addrmode_u = desc.addrmode_v = desc.addrmode_w = GL_CLAMP_TO_BORDER;
		a.res.blur_read_sampler = eng::gl().bs.get_sampler_object(desc);
	}
}

void load_shader_programs(Demo &a)
{
	// Rendering shaders

	g_shader_defs.add("PARTICLE_SIZE", gconf.particle_size);
	g_shader_defs.add("CAMERA_UBLOCK_BINDPOINT", (int)a.res.camera_ubo.bindpoint());
	g_shader_defs.add("SIM_CONSTANTS_UBO_BINDPOINT", (int)a.res.sim_constants_ubo_binding.bindpoint());

	LOG_F(INFO, "Compiling compute shader with macros defined - \n%s", g_shader_defs.get_string().c_str());

	fn_ create_vsfs_program = [&](const char *file_name) {
		VSFS_Program p;
		g_shader_defs.add("DO_VS", 1);
		p.vs = create_shader_from_big_file(file_name, eng::VERTEX_SHADER, g_shader_defs);
		g_shader_defs.remove("DO_VS").add("DO_FS", 1);
		p.fs = create_shader_from_big_file(file_name, eng::FRAGMENT_SHADER, g_shader_defs);
		g_shader_defs.remove("DO_FS");
		p.program = eng::create_program(p.vs, p.fs);
		return p;
	};

	a.res.record_attribute_prog = create_vsfs_program("record_attribute_vsfs.glsl");
	a.res.blur_only_density_prog = create_vsfs_program("blur_density_vsfs.glsl");
	a.res.color_density_prog = create_vsfs_program("color_output_vsfs.glsl");

	// Compute shaders. Keeping all the shaders in one glsl file. Using macros to selectively create a full
	// shader.
	{
		eng::ShaderDefinesRAII selector(g_shader_defs);
		selector.add("INTEGRATION_KERNEL");

		// LOG_F(INFO, "Compiling compute shader with macros defined - \n%s",
		// g_shader_defs.get_string().c_str());

		a.res.integrate_csprog =
		  create_compute_shader_program("fluidcs11.comp", g_shader_defs, "@integrate_cs");
	}

	{
		eng::ShaderDefinesRAII selector(g_shader_defs);
		selector.add("DENSITY_SHAREDMEM_KERNEL");
		a.res.density_shared_csprog =
		  create_compute_shader_program("fluidcs11.comp", g_shader_defs, "@density_shared_cs");
	}

	{
		eng::ShaderDefinesRAII selector(g_shader_defs);
		selector.add("FORCE_SHAREDMEM_KERNEL");

		a.res.force_shared_csprog =
		  create_compute_shader_program("fluidcs11.comp", g_shader_defs, "@force_shared_cs");
	}

	{
		eng::ShaderDefinesRAII selector(g_shader_defs);
		selector.add("DENSITY_SIMPLE_KERNEL");

		// LOG_F(INFO, "Compiling compute shader with macros defined - \n%s",
		// g_shader_defs.get_string().c_str());

		a.res.density_simple_csprog =
		  create_compute_shader_program("fluidcs11.comp", g_shader_defs, "@density_simple_cs");
	}
	{
		eng::ShaderDefinesRAII selector(g_shader_defs);
		selector.add("FORCE_SIMPLE_KERNEL");

		// LOG_F(INFO, "Compiling compute shader with macros defined - \n%s",
		// g_shader_defs.get_string().c_str());

		a.res.force_simple_csprog =
		  create_compute_shader_program("fluidcs11.comp", g_shader_defs, "@force_simple_cs");
	}

	{
		eng::ShaderDefinesRAII selector(g_shader_defs);
		selector.add("ADVANCE_KERNEL");

		// LOG_F(INFO, "Compiling compute shader with macros defined - \n%s",
		// g_shader_defs.get_string().c_str());

		a.res.dbg_advance_csprog =
		  create_compute_shader_program("fluidcs11.comp", g_shader_defs, "@dbg_advance_cs");
	}

#if 0
    {
        eng::ShaderDefinesRAII selector(defs);
        selector.add("BUILD_GRID_KERNEL");
        a.res.build_grid_csprog =
            create_compute_shader_program("fluidcs11.comp", defs, "@build_grid_cs");
    }
    {
        eng::ShaderDefinesRAII selector(defs);
        selector.add("CLEAR_GRID_INDICES_KERNEL");
        a.res.clear_grid_indices_csprog = create_compute_shader_program(
            "fluidcs11.comp", defs, "@clear_grid_indices_cs");
    }
    {
        eng::ShaderDefinesRAII selector(defs);
        selector.add("BUILD_GRID_INDICES_KERNEL");
        a.res.clear_grid_indices_csprog = create_compute_shader_program(
            "fluidcs11.comp", defs, "@build_grid_indices_cs");
    }
#endif
}

fo::Array<Particle> make_initial_particles()
{
	fo::Array<Particle> initial_particles_state(memory_globals::default_allocator());
	fo::resize(initial_particles_state, gconf.num_particles);
	LOG_F(INFO, "Initializing particles...");

	// Arrange particles in a square

	const u32 starting_width = (u32)std::sqrt((f32)gconf.num_particles);

	u32 x;
	u32 y;

	Vector2 offset = { gconf.map_width / 3.0f, gconf.map_height / 4.0f };

	for (u32 i = 0; i < gconf.num_particles; i++) {
		x = i % starting_width;
		y = i / starting_width;

		auto &p = initial_particles_state[i];

		p.position =
		  Vector2{ gconf.initial_particle_spacing * (f32)x, gconf.initial_particle_spacing * (f32)y } +
		  offset;
		p.velocity = zero_2;

#if 0
		if (i < 10) {
			LOG_F(INFO, "Particle %u, position = [%.2f, %.2f]", i, XY(p.position));
		}
#endif
	}

	return initial_particles_state;
}

void create_simulation_buffers(Demo &a)
{
	// Create each buffer and bind to the pipeline

	a.res.sim_constants_ubo_binding = create_and_bind_ubo(
	  a.gl->bs, sizeof(SimulationConstantsUB), GL_DYNAMIC_STORAGE_BIT, "@SimulationConstantsUB");

	LOG_F(INFO, "simulation constants bindpoint = %u", a.res.sim_constants_ubo_binding.bindpoint());

	// Create the initial state of the particles (position and velocity) and source into the GPU buffer.
	fo::Array<Particle> initial_particles_state(make_initial_particles());

	a.tmp_init_particles = initial_particles_state;

	LOG_F(INFO, "First particle position = [%.2f, %.2f]", XY(initial_particles_state[0].position));

	// constexpr GLbitfield particle_buffer_access = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
	constexpr GLbitfield particle_buffer_access = 0;

	a.res.particles_ssbo_binding = create_and_bind_ssbo<Particle>(a.gl->bs,
																  PARTICLES_RO,
																  gconf.num_particles,
																  particle_buffer_access,
																  "@particles_ssbo",
																  fo::data(initial_particles_state));
	a.ssbo_bound_at[PARTICLES_RO] = a.res.particles_ssbo_binding.handle();

	a.res.forces_ssbo_binding = create_and_bind_ssbo<ParticleForce>(
	  a.gl->bs, FORCES, gconf.num_particles, particle_buffer_access, "@particle_forces_ssbo", nullptr);

	a.ssbo_bound_at[FORCES] = a.res.forces_ssbo_binding.handle();

	a.res.densities_ssbo_binding = create_and_bind_ssbo<ParticleDensity>(
	  a.gl->bs, DENSITIES, gconf.num_particles, particle_buffer_access, "@particle_densities_ssbo", nullptr);
	a.ssbo_bound_at[DENSITIES] = a.res.densities_ssbo_binding.handle();

	a.res.sorted_particles_ssbo_binding = create_and_bind_ssbo<Particle>(
	  a.gl->bs, PARTICLES_RW, gconf.num_particles, particle_buffer_access, "@particles_sorted_ssbo", nullptr);

	a.ssbo_bound_at[PARTICLES_RW] = a.res.sorted_particles_ssbo_binding.handle();

	// Set macros for bindpoints

	g_shader_defs.add("PARTICLES_RO_BINDPOINT", (int)SsboBindpoints::PARTICLES_RO);
	g_shader_defs.add("PARTICLES_RW_BINDPOINT", (int)SsboBindpoints::PARTICLES_RW);
	g_shader_defs.add("FORCES_BINDPOINT", (int)SsboBindpoints::FORCES);
	g_shader_defs.add("DENSITIES_BINDPOINT", (int)SsboBindpoints::DENSITIES);

	// UBO for the blur parameters
	{
		a.res.blur_params_ubo_binding = create_and_bind_ubo(eng::gl().bs, sizeof(BlurParamsUB));
		g_shader_defs.add("DENSITY_BLUR_UBO_BINDPOINT", (int)a.res.blur_params_ubo_binding.bindpoint());
	}

	LOG_F(INFO, "Created simulation buffers - defs = \n%s", g_shader_defs.get_string().c_str());
}

void set_up_viewport_transforms(Demo &a)
{
	// Set up view transform. The camera (for now) is fixed. So directly setting it up. Change the position if
	// I want to.
	fo::Vector3 camera_pos = { 0.0f, 0.f, 2.0f };
	fo::Vector3 camera_fwd = normalize(zero_3 - camera_pos);
	fo::Vector3 camera_right = unit_x;
	fo::Vector3 camera_up = unit_y;

	fo::Matrix4x4 camera_axes = {
		vector4(camera_right), vector4(camera_up), -vector4(camera_fwd), point4(camera_pos)
	};

	a.camera_ub_data.view = inverse_rotation_translation(camera_axes);

	// particle width is same as the number of particles. Won't be much of an issue for now.
	// float world_width = (float)gconf.num_particles;

	f32 w = gconf.map_width;
	f32 h = gconf.map_height;

	// f32 w = DUMMY_MAP_WIDTH;
	// f32 h = DUMMY_MAP_HEIGHT;

	a.camera_ub_data.proj = ortho_proj(
	  { { 0, w }, { -1.0f, 1.0f } }, { { 0, h }, { -1.0f, 1.0f } }, { { 0.0f, -1000.0f }, { -1.0f, 1.0f } });

	// ^ Using near z = 0.0f will prevent clipping of the particle quads.

	a.camera_ub_data.camera_position = vector4(camera_pos);

	print_matrix_classic("View matrix", a.camera_ub_data.view);
	print_matrix_classic("Clip matrix", a.camera_ub_data.proj);

#if 1
	{
		u32 num_tested_points = 10;
		LOG_SCOPE_F(INFO, "Example transforms of first %u points...", num_tested_points);

		Matrix4x4 pv = a.camera_ub_data.proj * a.camera_ub_data.view;

		for (u32 i = 0; i < num_tested_points; ++i) {
			auto &pos = a.tmp_init_particles[i].position;
			Vector4 pos4 = { pos.x, pos.y, -1.0f, 1.0f };

			Vector4 tpos4 = pv * pos4;

			printf("[%.2f, %.2f] => [%.2f, %.2f, %.2f]\n", XY(pos), XYZ(tpos4));
		}
	}
#endif
}

void simulate_fluid_simple(Demo &a)
{
	// Unlike D3D, I can bind read only and read-write to the same point and use 'readonly' to indicate the
	// accessibility. This is fine if I don't access both of the blocks from the same shader.

	eng::gl_timer_query::new_frame(a.timer_manager);
	DEFERSTAT(eng::gl_timer_query::end_frame(a.timer_manager));

	eng::gl_timer_query::begin_timer(a.timer_manager, a.timer_for_simulate);
	DEFERSTAT(eng::gl_timer_query::end_timer(a.timer_manager, a.timer_for_simulate));

#if 1
	bind_ssbo(a, a.res.particles_ssbo_binding, PARTICLES_RO, gconf.num_particles * sizeof(Particle));
	bind_ssbo(a, a.res.densities_ssbo_binding, DENSITIES, gconf.num_particles * sizeof(ParticleDensity));
	bind_ssbo(a, a.res.forces_ssbo_binding, FORCES, gconf.num_particles * sizeof(ParticleForce));

	// Compute densities

	glUseProgram(a.res.density_simple_csprog.program);
	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// Compute forces

	glUseProgram(a.res.force_simple_csprog.program);
	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// Update particles. In this simple algo we copy the current state of particles into the sorted particles
	// buffer before running the integration.
	glCopyNamedBufferSubData(a.res.particles_ssbo_binding.handle(),
							 a.res.sorted_particles_ssbo_binding.handle(),
							 0,
							 0,
							 gconf.num_particles * sizeof(Particle));
	bind_ssbo(a, a.res.particles_ssbo_binding, PARTICLES_RW, gconf.num_particles * sizeof(Particle));
	bind_ssbo(a,
			  a.res.sorted_particles_ssbo_binding,
			  PARTICLES_RO,
			  gconf.num_particles * sizeof(Particle)); // @rksht - Do ping pong instead of copy buffer data?

	glUseProgram(a.res.integrate_csprog.program);
	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);

#else
	glUseProgram(a.res.dbg_advance_csprog.program);
	bind_ssbo(a, a.res.particles_ssbo_binding, PARTICLES_RO, gconf.num_particles * sizeof(Particle));
	bind_ssbo(a, a.res.sorted_particles_ssbo_binding, PARTICLES_RW, gconf.num_particles * sizeof(Particle));

	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	glCopyNamedBufferSubData(a.res.sorted_particles_ssbo_binding.handle(),
							 a.res.particles_ssbo_binding.handle(),
							 0,
							 0,
							 gconf.num_particles * sizeof(Particle));

#endif
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

void simulate_fluid_using_sharedmem(Demo &a)
{
	eng::gl_timer_query::new_frame(a.timer_manager);
	DEFERSTAT(eng::gl_timer_query::end_frame(a.timer_manager));

	eng::gl_timer_query::begin_timer(a.timer_manager, a.timer_for_simulate);
	DEFERSTAT(eng::gl_timer_query::end_timer(a.timer_manager, a.timer_for_simulate));

	bind_ssbo(a, a.res.particles_ssbo_binding, PARTICLES_RO, gconf.num_particles * sizeof(Particle));
	bind_ssbo(a, a.res.densities_ssbo_binding, DENSITIES, gconf.num_particles * sizeof(ParticleDensity));
	bind_ssbo(a, a.res.forces_ssbo_binding, FORCES, gconf.num_particles * sizeof(ParticleForce));

	// Compute densities

	glUseProgram(a.res.density_shared_csprog.program);
	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// Compute forces

	glUseProgram(a.res.force_shared_csprog.program);
	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	// Update particles. In this simple algo we copy the current state of particles into the sorted particles
	// buffer before running the integration. The particles are not actually 'sorted' in this version.
	glCopyNamedBufferSubData(a.res.particles_ssbo_binding.handle(),
							 a.res.sorted_particles_ssbo_binding.handle(),
							 0,
							 0,
							 gconf.num_particles * sizeof(Particle));
	bind_ssbo(a, a.res.particles_ssbo_binding, PARTICLES_RW, gconf.num_particles * sizeof(Particle));
	bind_ssbo(a,
			  a.res.sorted_particles_ssbo_binding,
			  PARTICLES_RO,
			  gconf.num_particles * sizeof(Particle)); // @rksht - Do ping pong instead of copy buffer data?

	glUseProgram(a.res.integrate_csprog.program);
	glDispatchCompute(gconf.num_particles / SIMULATION_BLOCK_SIZE, 1, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

void simulate_fluid_using_grid(Demo &a) {
}

void source_simulation_ubo(Demo &a, f32 frame_time)
{

	static int once = 0;

	SimulationConstantsUB constants = {};

	constants.num_particles = gconf.num_particles;
	// timestep is clamped to prevent numerical explosion
	constants.timestep = std::min(gconf.max_allowable_timestep, frame_time);
	if (constants.timestep < 0.1) {
		constants.timestep = 0.016f;
	}
	constants.smooth_len = gconf.smooth_len;
	constants.pressure_stiffness = gconf.pressure_stiffness;
	constants.rest_density = gconf.rest_density;

	constants.density_coef = gconf.particle_mass * 315.0f / (64.0f * pi * pow(gconf.smooth_len, 9));
	constants.grad_pressure_coef = gconf.particle_mass * -45.0f / (pi * pow(gconf.smooth_len, 6));
	constants.lap_viscosity_coef =
	  gconf.particle_mass * gconf.viscosity * 45.0f / (pi * pow(gconf.smooth_len, 6.0f));

	constants.gravity = Vector4(gconf.gravity.x, gconf.gravity.y, 0.0f, 0.0f);

	// Grid cells are spaced by a distance equal to the smoothing length along both x and y.
	constants.grid_dim.x = 1.f / gconf.smooth_len;
	constants.grid_dim.y = 1.f / gconf.smooth_len;
	constants.grid_dim.z = 0.f;
	constants.grid_dim.w = 0.f;

	constants.wall_stiffness = gconf.wall_stiffness;
	std::copy(gconf.planes.cbegin(), gconf.planes.cend(), constants.planes);

	constants.screen_wh.x = (f32)glparams.window_width;
	constants.screen_wh.y = (f32)glparams.window_height;

	constants.particle_render_size = gconf.particle_render_size;

	a.res.sim_constants_ubo_binding.bind();

	// Update simulation constants ubuffer
	glInvalidateBufferData(a.res.sim_constants_ubo_binding.handle());
	glNamedBufferSubData(
	  a.res.sim_constants_ubo_binding.handle(), 0, sizeof(SimulationConstantsUB), &constants);

	if (!once) {
		once = 1;

		LOG_F(INFO,
			  R"(
            Simulation constants
            ====================

    num_particles = %u
    timestep = %.3f
    smooth_len = %.4f
    pressure_stiffness = %.4f
    rest_density = %.4f
    density_coeff = %.4f
    grad_pressure_coef = %.4f
    lap_viscosity_coef = %.4f
    wall_stiffness = %.4f
    gravity = [%.2f, %.2f, %.2f, %.2f]
    grid_dim = [%.2f, %.2f, %.2f, %.2f]
    planes[0] = [%.2f, %.2f, %.2f, %.2f]
    planes[1] = [%.2f, %.2f, %.2f, %.2f]
    planes[2] = [%.2f, %.2f, %.2f, %.2f]
    planes[3] = [%.2f, %.2f, %.2f, %.2f]

        )",
			  constants.num_particles,
			  constants.timestep,
			  constants.smooth_len,
			  constants.pressure_stiffness,
			  constants.rest_density,
			  constants.density_coef,
			  constants.grad_pressure_coef,
			  constants.lap_viscosity_coef,
			  constants.wall_stiffness,
			  XYZW(constants.gravity),
			  XYZW(constants.grid_dim),
			  XYZW(constants.planes[0]),
			  XYZW(constants.planes[1]),
			  XYZW(constants.planes[2]),
			  XYZW(constants.planes[3]));
	}
}

void simulate_fluid(Demo &a, f32 frame_time)
{
	// Fill the constants buffer
	source_simulation_ubo(a, frame_time);

	switch (a.cur_simulation_mode) {
	case SimulationMode::SIMPLE: {
		simulate_fluid_simple(a);
	} break;
	case SimulationMode::SHARED: {
		simulate_fluid_using_sharedmem(a);
	} break;
	default: {
		CHECK_F(false, "Not implemented yet!!");
	}
	}

	// Unset the shaders and buffer bindings...? Maybe not now.
}

void create_camera_ubo(Demo &a)
{
#if 0
	a.res.camera_ubo = create_and_bind_ubo(
	  a.gl->bs, sizeof(eng::CameraTransformUB), GL_DYNAMIC_STORAGE_BIT, "@Camera_Ublock", &a.camera_ub_data);

	GLuint ubo_handle = eng::get_gluint_from_rmid(eng::g_rm(), eng::g_rm().camera_ubo_handle().rmid());

	glBindBuffer(GL_UNIFORM_BUFFER, ubo_handle);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(eng::CameraTransformUB), &a.camera_ub_data);

#else

	GLuint ubo_handle = eng::get_gluint_from_rmid(eng::g_rm(), eng::g_rm().camera_ubo_handle().rmid());
	LOG_F(INFO, "Camera UBO = %u", ubo_handle);

	a.res.camera_ubo =
	  std::make_pair(eng::gl_desc::UniformBuffer(ubo_handle, 0, sizeof(eng::CameraTransformUB)), GLuint(0));

	glBindBuffer(GL_UNIFORM_BUFFER, ubo_handle);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(eng::CameraTransformUB), &a.camera_ub_data);

#endif

	LOG_F(INFO, "camera ubo bindpoint = %u", a.res.camera_ubo.bindpoint());
}

void set_one_time_render_states(Demo &a)
{
	eng::gl().bs.set_rasterizer_state(a.rast_state_particles);
	eng::gl().bs.set_depth_stencil_state(a.ds_state_particles);
	// eng::gl().default_fbo.set_blend_config(0, a.blend_config_particles);
	eng::gl().bs.set_blend_function(0, a.blend_config_particles);
}

void init_from_ini(Demo &a)
{
#define INI_IF(key, method_call, variable_to_store_in) method_call(key, variable_to_store_in)

	INI_IF("particle_size", a.ini_conf.number, gconf.particle_size);
	INI_IF("initial_particle_spacing", a.ini_conf.number, gconf.initial_particle_spacing);
	INI_IF("num_particles", a.ini_conf.number, gconf.num_particles);
	INI_IF("viscosity", a.ini_conf.number, gconf.viscosity);
	INI_IF("wall_stiffness", a.ini_conf.number, gconf.wall_stiffness);
	INI_IF("gravity_y", a.ini_conf.number, gconf.gravity.y);
	INI_IF("smooth_len", a.ini_conf.number, gconf.smooth_len);
	INI_IF("pressure_stiffness", a.ini_conf.number, gconf.pressure_stiffness);
	INI_IF("gaussian_kernel_size", a.ini_conf.number, gconf.gaussian_kernel_size);

	LOG_F(INFO, "Number of particles = %u", gconf.num_particles);
	LOG_F(INFO, "Particle size = %.5f", gconf.particle_size);
	LOG_F(INFO, "Particle spacing = %.5f", gconf.initial_particle_spacing);

	LOG_F(INFO, "viscosity = %.4f", gconf.viscosity);
	LOG_F(INFO, "wall_stiffness= %.5f", gconf.wall_stiffness);
	LOG_F(INFO, "gravity_y= %.5f", gconf.gravity.y);
	LOG_F(INFO, "smooth_len= %.5f", gconf.smooth_len);
}

#if 0
void test_vertex_shader(Demo &a) {

    fn_ vs_func = [&](u32 vid) {
        u32 particle_index = vid / 6;
        u32 vertex_in_quad = vid % 6;

        Vector2 v_pos;

        // Get the vertex's position in the unit quad
        v_pos.x = (2u <= vertex_in_quad && vertex_in_quad <= 4) ? 1.0f : -1.0f;
        v_pos.y = (1u <= vertex_in_quad && vertex_in_quad <= 3) ? -1.0f : 1.0f;

        v_pos = gconf.particle_size * v_pos;

        // Map-space position
        // v_pos = a.tmp_init_particles[particle_index].position + v_pos;

        // Vector4 gl_Position = a.camera_ub_data.clip_from_view_xform *
        // a.camera_ub_data.view_from_world_xform * Vector4(XY(v_pos), 0, 1);

        Vector4 gl_Position = a.camera_ub_data.view_from_world_xform * Vector4(XY(v_pos), 0, 1);
        // Vector4 gl_Position = Vector4(XY(v_pos), 0, 1);

        return gl_Position;
    };

    fn_ draw_quad = [&](u32 pid) {
        Vector4 quad[6] = {};

        for (u32 vid = pid * 6, i = 0; i < 6; ++vid, ++i) {
            quad[i] = vs_func(vid);
        }

        LOG_F(INFO,
              R"(
            Particle %u 's (map position = [%.2f, %.2f]) quad =
        [%.4f, %.4f, %.4f, %.4f] ------------------------- [%.4f, %.4f, %.4f, %.4f]
                |                                              |
                |                                              |
                |                                              |
                |                                              |
                |                                              |
                |                                              |
                |                                              |
        [%.4f, %.4f, %.4f, %.4f] ------------------------- [%.4f, %.4f, %.4f, %.4f]
        )",
              pid,
              XY(a.tmp_init_particles[pid].position),
              XYZW(quad[0]),
              XYZW(quad[4]),
              XYZW(quad[1]),
              XYZW(quad[2]));
    };

    // debug_break();

    u32 num_particles_to_test = (u32)a.ini_conf.number("num_particles_to_test");

    for (u32 pid = 0; pid < num_particles_to_test; ++pid) {
        draw_quad(pid);
    }
}

#endif

void create_offscreen_framebuffers(Demo &a)
{
	eng::SamplerDesc sampler_desc = eng::default_sampler_desc;
	sampler_desc.min_filter = GL_NEAREST;
	sampler_desc.mag_filter = GL_NEAREST;

	// Framebuffer to hold a particular attribute in screen space
	GLuint f32_attrib_texture, depth_rbo;
	glGenTextures(1, &f32_attrib_texture);
	glGenRenderbuffers(1, &depth_rbo);

	glBindTexture(GL_TEXTURE_2D, f32_attrib_texture);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, glparams.window_width, glparams.window_height);
	eng::set_texture_label(f32_attrib_texture, "@particle_attribs");
	eng::set_texture_parameters(f32_attrib_texture, sampler_desc);

	glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, glparams.window_width, glparams.window_height);

	a.screen_space_attrib_fbo.gen("@ss_attrib_fbo")
	  .bind()
	  .add_attachment(0, f32_attrib_texture)
	  .add_depth_attachment_rbo(depth_rbo)
	  .set_draw_buffers({ 0 })
	  .bind_as_readable()
	  .set_read_buffer(0)
	  .bind_as_writable()
	  .set_done_creating();

	// Two density fbos for holding the blur result. Two because we are using separable gaussian kernel and
	// want to run 2 passes. Render particle attributes to a screen space vec4. Right now, I'm rendering quite
	// a few attributes, not just density. So do need to copy the densities first to a density-only FBO.
	// Render density to fbo_0. (Could avoid it, but nah). Blur horizontally fbo_0 and render to fbo_1. Blur
	// vertically fbo_1 and render to fbo_0. fbo_0 therefore contains the final blurred result. We
	a.density_blur_fbo.gen("@blur_fbo").bind();

	GLuint r32f_textures[2];

	for (int i = 0; i < 2; ++i) {
		glGenTextures(1, &r32f_textures[i]);
		glBindTexture(GL_TEXTURE_2D, r32f_textures[i]);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, glparams.window_width, glparams.window_height);
		eng::set_texture_label(r32f_textures[i], fmt::format("@density_blur_texture_{}", i).c_str());
		a.density_blur_fbo.add_attachment(i, r32f_textures[i]);

		eng::set_texture_parameters(r32f_textures[i], sampler_desc);
	}

	a.density_blur_fbo.set_done_creating();
}

void render_blurred_density_field(Demo &a)
{
	// Record attribute in screen space, attribute being the density, into screen space :

	eng::gl().bs.set_rasterizer_state(a.rast_state_particles);
	eng::gl().bs.set_depth_stencil_state(a.ds_state_particles);
	// eng::gl().bs.set_blend_function(0, a.blend_config_particles);

	eng::gl().bs.set_blend_function(0, eng::default_blendfunc_state);

	glDisable(GL_SCISSOR_TEST);

	//      FBO setup:

	eng::SetInputOutputFBO fbo_inout;

	fbo_inout.input_fbo = &eng::g_bs()._screen_fbo;
	fbo_inout.output_fbo = &a.screen_space_attrib_fbo;
	fbo_inout.read_attachment_number = 0;
	fbo_inout.set_attachment_of_output(0, 0);
	eng::set_input_output_fbos(fbo_inout);
	// a.screen_space_attrib_fbo.bind_as_writable().clear_color(0, zero_4).clear_depth(1.0f);
	a.screen_space_attrib_fbo.clear_color(0, zero_4).clear_depth(1.0f); // clear_depth not needed

	// glDisable(GL_BLEND);

	//      Set Program and draw:
	glUseProgram(a.res.record_attribute_prog.program);

	a.res.camera_ubo.bind();
	glNamedBufferSubData(a.res.camera_ubo.handle(), 0, sizeof(eng::CameraTransformUB), &a.camera_ub_data);

	glBindVertexArray(eng::gl().bs.no_attrib_vao());
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindVertexBuffer(0, 0, 0, 0);
	glDrawArrays(GL_TRIANGLES, 0, gconf.num_particles * 6);

	// Blur the attribute values vertically:
	//
	//      Read buffer = screen_space_attrib_fbo's color texture
	//      Draw buffer = density_blur_fbo's 0 color texture
	fbo_inout.reset_struct();
	fbo_inout.input_fbo = &a.screen_space_attrib_fbo;
	fbo_inout.output_fbo = &a.density_blur_fbo;
	fbo_inout.read_attachment_number = 0;
	fbo_inout.set_attachment_of_output(0, 0);
	eng::set_input_output_fbos(fbo_inout);
	a.density_blur_fbo.clear_color(0, zero_4);
	// a.density_blur_fbo.bind_as_writable(GLuint(a.screen_space_attrib_fbo)).set_draw_buffers({0,
	// eng::FBO::NO_ATTACHMENT});

	//      Set program
	glUseProgram(a.res.blur_only_density_prog.program);

	a.res.blur_params_ubo_binding.bind();

	glBindSampler(0, a.res.blur_read_sampler);
	glBindTextureUnit(0, a.screen_space_attrib_fbo.color_attachment_texture(0));

	//      Notify shader we are blurring horizontally
	u32 b_horizontal_or_vertical = 1;
	// bool uniform to note that we are using vertical offsets for samples
	glInvalidateBufferData(a.res.blur_params_ubo_binding.handle());
	glNamedBufferSubData(a.res.blur_params_ubo_binding.handle(), 0, sizeof(u32), &b_horizontal_or_vertical);

	//      Draw full screen triangle
	glDrawArrays(GL_TRIANGLES, 0, 3);

	//          Blur horizontally:

	//          Read buffer = density_blur_fbo's 0 color texture
	//          Draw buffer = density_blur_fbo's 1 color texture
	fbo_inout.reset_struct();
	fbo_inout.input_fbo = &a.density_blur_fbo;
	fbo_inout.output_fbo = &a.density_blur_fbo;
	fbo_inout.read_attachment_number = 0;
	fbo_inout.set_attachment_of_output(0, 1);
	eng::set_input_output_fbos(fbo_inout);
	// a.density_blur_fbo.set_read_buffer(0).set_draw_buffers({1, eng::FBO::NO_ATTACHMENT});

	glBindTextureUnit(0, a.density_blur_fbo.color_attachment_texture(0));

	b_horizontal_or_vertical = 0;
	glInvalidateBufferData(a.res.blur_params_ubo_binding.handle());
	glNamedBufferSubData(a.res.blur_params_ubo_binding.handle(), 0, sizeof(u32), &b_horizontal_or_vertical);

	//      Draw full screen triangle
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Blit the texture to the screen framebuffer.

	// Render the density into screen with color
	fbo_inout.reset_struct();
	fbo_inout.input_fbo = &a.density_blur_fbo;
	// fbo_inout.input_fbo = &eng::g_bs()._screen_fbo;
	fbo_inout.output_fbo = &eng::g_bs()._screen_fbo;
	fbo_inout.read_attachment_number = 1;
	fbo_inout.set_attachment_of_output(0, 0);
	eng::set_input_output_fbos(fbo_inout);
	// a.density_blur_fbo.bind_as_readable((GLuint)eng::gl().bs._screen_fbo).set_read_buffer(1);

	// eng::gl().bs._screen_fbo.clear_color(0, colors::Black).clear_depth(1.0f); // Clear depth not needed

	glUseProgram(a.res.color_density_prog.program);
	glBindTextureUnit(0, a.density_blur_fbo.color_attachment_texture(1));

	//      Draw full screen triangle
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

#if 0
    glBlitNamedFramebuffer(a.density_blur_fbo._fbo_handle, GLuint(eng::gl().bs._screen_fbo), 0, 0,
                           glparams.window_width, glparams.window_height, 0, 0, glparams.window_width,
                           glparams.window_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

#endif
}

void render_imgui(Demo &a)
{
	ImGui::Render();
	imgl3_render_draw_data(ImGui::GetDrawData());
}

void create_gl_timers(Demo &a)
{
	a.timer_for_simulate = eng::gl_timer_query::add_timer(a.timer_manager, "timer.simulate");
	a.timer_for_render = eng::gl_timer_query::add_timer(a.timer_manager, "timer.render");
	a.timer_for_blit = eng::gl_timer_query::add_timer(a.timer_manager, "timer.blit");
	eng::gl_timer_query::done_adding(a.timer_manager);
	eng::gl_timer_query::set_no_warning(a.timer_manager);
}

void build_blur_kernel(Demo &a)
{
	auto binomial_coeffs = gen_binomial_coefficients(9);
	std::vector<float> weights(binomial_coeffs.size());
	std::transform(binomial_coeffs.begin(), binomial_coeffs.end(), weights.begin(), [](int coeff) {
		return float(coeff) / (1u << 9);
	});

	for (float weight : weights) {
		printf("%.5f, ", weight);
	}

	puts("\n");

	float normalized_texel_width = f32(1.0 / (double)glparams.window_width);
	float normalized_texel_height = f32(1.0 / (double)glparams.window_height);

	// Add the parameters to the shader defs
	g_shader_defs.add("GAUSSIAN_KERNEL_SIZE", (int)binomial_coeffs.size());
	g_shader_defs.add("GAUSSIAN_KERNEL_WEIGHTS", weights);

	g_shader_defs.add("NORMALIZED_TEXEL_WIDTH", normalized_texel_width);
	g_shader_defs.add("NORMALIZED_TEXEL_HEIGHT", normalized_texel_height);
}

namespace app_loop
{

	template <> void init<Demo>(Demo &a)
	{
		init_from_ini(a);

		ImGui::CreateContext();

		imglfw_init(a.gl->window);
		imgl3_init();

		build_blur_kernel(a);

		create_simulation_buffers(a);
		set_up_viewport_transforms(a);
		create_camera_ubo(a);
		create_render_states(a);
		load_shader_programs(a);

		set_one_time_render_states(a);

		source_simulation_ubo(a, 0);

		create_offscreen_framebuffers(a);

		create_gl_timers(a);

		a.gl->bs.print_log();

		// test_vertex_shader(a);
	}

	template <> void close<Demo>(Demo &app)
	{
		fo::string_stream::Buffer ss;
		eng::gl_timer_query::print_times(app.timer_manager, ss);

		LOG_F(INFO, "Timers --- \n%s", fo::string_stream::c_str(ss));

		imgl3_shutdown();
		imglfw_shutdown();
	}

	template <> void update(Demo &app, State &loop_timer)
	{
		glfwPollEvents();

		imglfw_new_frame();
		imgl3_new_frame();
		ImGui::NewFrame();

#if 0
		if (app.show_imgui_debug_window) {
			ImGui::ShowDemoWindow(&app.show_imgui_debug_window);
		}
#endif

		// Configuration input window
		static bool show_config_window = true;

		if (show_config_window) {
			ImGui::Begin("Config window", &show_config_window);
			DEFERSTAT(ImGui::End());

			// Simulation mode
			::optional<SimulationMode> chosen_simulation_mode;

			if (ImGui::RadioButton("Simple", app.cur_simulation_mode == SimulationMode::SIMPLE)) {
				chosen_simulation_mode = SimulationMode::SIMPLE;
			}

			ImGui::SameLine();

			if (ImGui::RadioButton("Shared", app.cur_simulation_mode == SimulationMode::SHARED)) {
				chosen_simulation_mode = SimulationMode::SHARED;
			}

			ImGui::SameLine();

			if (ImGui::RadioButton("Grid", app.cur_simulation_mode == SimulationMode::GRID)) {
				chosen_simulation_mode = SimulationMode::GRID;
			}

			if (chosen_simulation_mode && chosen_simulation_mode.value() != app.cur_simulation_mode) {
				LOG_F(INFO, "Changing simulation mode");
				app.cur_simulation_mode = chosen_simulation_mode.value();
			}

			ImGui::DragFloat("particle-render-size",
							 &gconf.particle_render_size,
							 0.001,
							 PARTICLE_RENDER_SIZE_MIN_MAX.first,
							 PARTICLE_RENDER_SIZE_MIN_MAX.second,
							 "%.3f");

			gconf.particle_render_size = clamp(gconf.particle_render_size,
											   PARTICLE_RENDER_SIZE_MIN_MAX.first,
											   PARTICLE_RENDER_SIZE_MIN_MAX.second);

			ImGui::DragFloat(
			  "viscosity", &gconf.viscosity, 0.01, VISCOSITY_MIN_MAX.first, VISCOSITY_MIN_MAX.second, "%.3f");

			gconf.viscosity = clamp(gconf.viscosity, VISCOSITY_MIN_MAX.first, VISCOSITY_MIN_MAX.second);
		}

		simulate_fluid(app, (f32)loop_timer.frame_time_in_sec);
	}

	template <> void render(Demo &app)
	{

		render_blurred_density_field(app);

		// ImGui::EndFrame();

		render_imgui(app);

		glfwSwapBuffers(app.gl->window);

#if 0

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Use the vertex shader fully to generate the quad vertices per particle. We call Draw with null vbo/ebo,
    // and use VertexID to identitfy the particle number and the vertex number in the quad.

    glUseProgram(app.res.record_attribute_prog.program);

    glBindVertexArray(eng::gl().bs.no_attrib_vao());

    glDrawArrays(GL_TRIANGLES, 0, gconf.num_particles * 6); // 6 verts per quad

    glfwSwapBuffers(app.gl->window);

#endif
	}

	template <> bool should_close(Demo &app)
	{
		return app.escape_pressed || glfwWindowShouldClose(app.gl->window);
	}

} // namespace app_loop

int main(int ac, char **av)
{
	eng::init_memory();
	DEFERSTAT(eng::shutdown_memory());

	init_gl_params();

	eng::start_gl(glparams, eng::gl());

	DEFERSTAT(eng::close_gl(glparams, eng::gl()));

	{
		Demo app;
		app.gl = &eng::gl();

		app_loop::State timer = {};
		app_loop::run(app, timer);
	}
}
