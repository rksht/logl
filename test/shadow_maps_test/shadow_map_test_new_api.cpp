#include "interim_fbo.h"

#include <learnogl/gl_misc.h>

#include <halpern_pmr/pmr_map.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/callstack.h>
#include <learnogl/eye.h>
#include <learnogl/gl_timer_query.h>
#include <learnogl/glsl_inspect.h>
#include <learnogl/input_handler.h>
#include <learnogl/renderdoc_app.h>
#include <learnogl/stb_image.h>

GLOBAL_STORAGE(int, the_int);

using namespace eng;
using namespace eng::math;

namespace constants
{

	constexpr u32 light_count = 4;
	constexpr i32 window_width = 1280;
	constexpr i32 window_height = 720;

} // namespace constants

struct ShapeSphere {
	fo::Vector3 position;
	f32 radius;
};

struct ShapeCube {
	fo::Vector3 position;
	fo::Vector3 extent;
};

using RenderableShape = ::VariantTable<ShapeSphere, ShapeCube>;

struct Material {
	fo::Vector4 diffuse_albedo;
	fo::Vector3 fresnel_R0;
	f32 shininess;
};

struct PerObjectData {
	fo::Matrix4x4 world_from_local;
	fo::Matrix4x4 world_from_local_inv;
	Material material;
};

struct RenderableData {
	// Everything in this struct remains constant once initialized, except the transforms for moving objects.
	VertexBufferHandle vbo_handle;
	IndexBufferHandle ebo_handle;
	VertexArrayHandle vao_handle;
	Texture2DHandle texture_handle;

	u32 packed_attr_size = 0;
	u32 num_indices = 0;

	// per object uniform data
	PerObjectData per_object_uniform_data = { eng::math::identity_matrix, eng::math::identity_matrix, {} };
};

struct DirLightInfo {
	alignas(16) fo::Vector3 position;
	alignas(16) fo::Vector3 direction;
	alignas(16) fo::Vector3 ambient;
	alignas(16) fo::Vector3 diffuse;
	alignas(16) fo::Vector3 specular;

	DirLightInfo() = default;

	DirLightInfo(fo::Vector3 position,
				 fo::Vector3 direction,
				 fo::Vector3 ambient,
				 fo::Vector3 diffuse,
				 fo::Vector3 specular)
		: position(position)
		, direction(direction)
		, ambient(ambient)
		, diffuse(diffuse)
		, specular(specular)
	{
		normalize_update(this->direction);
	}
};

struct ShadowRelatedUniforms {
	fo::Matrix4x4 shadow_xform;
	BoundingSphere scene_bs;
};

namespace shadow_map
{
	struct InitInfo {
		fo::Vector3 light_direction;
		fo::Vector3 light_position;
		u32 texture_size;
		f32 x_extent;
		f32 y_extent;
		f32 neg_z_extent;
	};

	struct ShadowMap {
		Texture2DHandle depth_texture;

		FboId the_fbo;

		fo::Vector3 light_position;
		fo::Vector3 light_direction;

		u16 texture_size;

		CameraTransformUB cam_transform_data; // Here the 'camera' is actually the light
	};

	void init(ShadowMap &m, const InitInfo &init_info) {}

	void set_as_draw_fbo(const ShadowMap &m, const FBO &read_fbo) {}

	void set_as_read_fbo(const ShadowMap &m, const FBO &draw_fbo) {}

	void bind_comparing_sampler(ShadowMap &m) {}

	void unbind_comparing_sampler(ShadowMap &m) {}

	// Clears the depth texture (fills with 1.0f)
	void clear(ShadowMap &m);

	inline const fo::Matrix4x4 &light_from_world_xform(const ShadowMap &m)
	{
		return m.cam_transform_data.view;
	}

	inline const fo::Matrix4x4 &clip_from_light_xform(const ShadowMap &m)
	{
		return m.cam_transform_data.proj;
	}

	// Returns a transform that takes a point in world space to clip space from light's point of view.
	fo::Matrix4x4 clip_from_world_xform(ShadowMap &m);

} // namespace shadow_map

// Defining the scene objects using 'shapes', then we make a mesh from the basic shape. This is easier to edit
// in code.
std::vector<RenderableShape> g_scene_objects = {
	{
	  ShapeSphere{ fo::Vector3{ 0.0f, 3.2f, -10.0f }, 3.0f },
	},
	{
	  ShapeSphere{ fo::Vector3{ 0.0f, 3.2f, 10.0f }, 3.0f },
	},
	{
	  ShapeSphere{ fo::Vector3{ 8.0f, 3.2f, 0.0f }, 3.0f },
	},
	{
	  ShapeSphere{ fo::Vector3{ -8.0f, 3.2f, 0.0f }, 3.0f },
	},
	{
	  ShapeCube{ fo::Vector3{ 0.0f, 0.0f, 0.0f }, fo::Vector3{ 15.0f, 0.2f, 15.0f } },
	},
	// ^ Floor
};

std::vector<DirLightInfo> g_dir_lights = {};

static float time_in_sec = 0.0f;

enum BlurKind : int {
	DO_NO_BLUR,
	DO_BLUR_1,
	DO_BLUR_2,
	BLUR_STYLE_COUNT,
};

static int g_blur_style = DO_NO_BLUR;

struct BindpointsInProgram {
	std::map<std::string, GLuint> uniform_blocks;
	std::map<std::string, GLuint> sampled_textures;
};

struct App {
	App(StringTable &string_table)
		: timer_manager(string_table)
	{
	}

	input::BaseHandlerPtr<App> input_handler = input::make_handler<input::InputHandlerBase<App>>();

	auto &current_input_handler() { return input_handler; }
	void set_input_handler(input::BaseHandlerPtr<App> ptr) { input_handler = std::move(ptr); }

	Camera camera;
	CameraTransformUB eye_block;
	CameraTransformUB eye_block_for_blitting;
	ShadowRelatedUniforms shadow_related;

	std::vector<RenderableData> opaque_renderables;
	std::vector<std::pair<size_t, size_t>> shape_ranges;

	BoundingSphere scene_bounding_sphere;

	RenderableData rd_scene_bs;
	RenderableData rd_casting_light;
	RenderableData rd_light_xyz;
	RenderableData rd_sphere_axes;
	RenderableData rd_light_box;
	// Full-screen quad for depth visualization
	RenderableData rd_screen_quad;

	struct {
		UniformBufferHandle camera_ubo;
		UniformBufferHandle dir_lights_list;
		UniformBufferHandle per_object_transforms;
		UniformBufferHandle shadow_transform;
		UniformBufferHandle tonemap_params;
	} uniform_buffers;

	struct {
		unsigned camera_ubo;
		unsigned dir_lights_list;
		unsigned per_object_transforms;
		unsigned shadow_transform;
	} uniform_bindpoints;

	struct {
		Texture2DHandle debug_box_texture;
		SamplerObjectHandle usual_sampler;
	} sampled_textures;

	struct {
		FboId shadow_map;
		FboId default_fbo;
		FboId fp16_light_output;
		FboId blur_src_dest;
	} render_targets;

	struct {
		VertexBufferHandle cube;
		VertexBufferHandle sphere;
	} vbos;

	struct {
		IndexBufferHandle cube;
		IndexBufferHandle sphere;
	} model_ebos = {};

	struct {
		mesh::StrippedMeshData cube;
		mesh::StrippedMeshData sphere;
	} stripped_meshes = {};

	struct {
		ShaderProgramHandle without_shadow;
		ShaderProgramHandle build_depth_map;
		ShaderProgramHandle blit_depth_map;
		ShaderProgramHandle with_shadow;
		ShaderProgramHandle basic_tonemap;
		ShaderProgramHandle png_blit;
	} shader_programs = {};

	BindpointsInProgram with_shadow_bindpoints;
	BindpointsInProgram without_shadow_bindpoints;
	BindpointsInProgram build_depth_map_bindpoints;
	BindpointsInProgram blit_depth_map_bindpoints;
	BindpointsInProgram basic_tonemap_bindpoints;
	BindpointsInProgram png_blit;

	shadow_map::ShadowMap shadow_map;

	ShaderDefines shader_defs;

	struct {
		u32 without_shadows;
		u32 first_pass;
		u32 second_pass;
	} rasterizer_states;

	bool window_should_close = false;

	FBO depth_map;

	eng::gl_timer_query::TimerQueryManager timer_manager;

	fs::path shaders_dir = fs::path(SOURCE_DIR) / "data";

	FP16ColorFBO hdr_color_fbo;
	PostProcess_GaussianBlur pp_gaussian_blur;
	FBO screen_fbo;

	bool do_blur = true;
	bool do_render_png_blit = true;
};

inline void update_camera_eye_block(App &app, float frame_time_in_sec)
{
	app.eye_block.camera_position = fo::Vector4(app.camera.position(), 1.0f);
	app.eye_block.view = app.camera.view_xform();
}

struct TonemapParamsUB {
	uint already_ldr;
};

// Creates all the uniform buffers and binds them to separate binding points.
void create_uniform_buffers(App &app)
{
	// Could create a single buffer and allocate blocks from there. But not bothering.
	{
		BufferCreateInfo buffer_ci;
		buffer_ci.bytes = sizeof(CameraTransformUB);
		buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
		buffer_ci.init_data = nullptr;
		buffer_ci.name = "@camera_ubo";
		app.uniform_buffers.camera_ubo = create_uniform_buffer(g_rm(), buffer_ci);
	}

	{
		constexpr size_t dir_lights_buffer_size = sizeof(DirLightInfo) * constants::light_count;

		BufferCreateInfo buffer_ci;
		buffer_ci.bytes = dir_lights_buffer_size;
		buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
		buffer_ci.init_data = nullptr;
		buffer_ci.name = "@dir_lights_list_ubo";

		app.uniform_buffers.dir_lights_list = create_uniform_buffer(g_rm(), buffer_ci);
	}

	{
		constexpr size_t per_object_buffer_size = sizeof(PerObjectData);

		BufferCreateInfo buffer_ci;
		buffer_ci.bytes = per_object_buffer_size;
		buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
		buffer_ci.init_data = nullptr;
		buffer_ci.name = "@per_object_data_ubo";

		app.uniform_buffers.per_object_transforms = create_uniform_buffer(g_rm(), buffer_ci);
	}

	{
		constexpr size_t shadow_related_buffer_size = sizeof(ShadowRelatedUniforms);

		BufferCreateInfo buffer_ci;
		buffer_ci.bytes = shadow_related_buffer_size;
		buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
		buffer_ci.init_data = nullptr;
		buffer_ci.name = "@shadow_related_ubo";
		app.uniform_buffers.shadow_transform = create_uniform_buffer(g_rm(), buffer_ci);
	}

	{
		const size_t tonemap_params_ubo_size = sizeof(TonemapParamsUB);

		BufferCreateInfo buffer_ci;
		buffer_ci.bytes = tonemap_params_ubo_size;
		buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
		buffer_ci.init_data = nullptr;
		buffer_ci.name = "@tonemap_params_ubo";

		app.uniform_buffers.tonemap_params = create_uniform_buffer(g_rm(), buffer_ci);
	}
}

void load_debug_box_texture(App &app)
{
	const auto file_path = make_path(generic_path(LOGL_DATA_DIR), "bowsette_1366x768.png");

	TextureCreateInfo texture_ci;

	texture_ci.source = file_path;
	texture_ci.texel_info.components = TexelComponents::RGBA;
	texture_ci.texel_info.internal_type = TexelOrigType::U8;
	texture_ci.texel_info.interpret_type = TexelInterpretType::NORMALIZED;

	app.sampled_textures.debug_box_texture = create_texture_2d(g_rm(), texture_ci);

	SamplerDesc sampler_desc = default_sampler_desc;
	app.sampled_textures.usual_sampler = create_sampler_object(g_rm(), sampler_desc);
}

void load_without_shadow_program(App &app)
{
	auto vs = create_shader_object(g_rm(),
								   app.shaders_dir / "usual_vs.vert",
								   ShaderKind::VERTEX_SHADER,
								   app.shader_defs,
								   "@usual_vs.vert")
				.get_value<VertexShaderHandle>();

	auto fs = create_shader_object(g_rm(),
								   app.shaders_dir / "without_shadow.frag",
								   ShaderKind::FRAGMENT_SHADER,
								   app.shader_defs,
								   "@without_shadow.frag")
				.get_value<FragmentShaderHandle>();

	app.shader_programs.without_shadow =
	  link_shader_program(g_rm(), ShadersToUse::from_vs_fs(vs.rmid(), fs.rmid()), "@without_shadow_prog");
}

void load_shadow_map_programs(App &app)
{
	const auto defs_str = app.shader_defs.get_string();

	// CHECK_F(false);

	VertexShaderHandle pos_only_vs = create_shader_object(g_rm(),
														  app.shaders_dir / "pos_only.vert",
														  ShaderKind::VERTEX_SHADER,
														  app.shader_defs,
														  "@pos_only.vert")
									   .get_value<VertexShaderHandle>();

	// Program that builds depth map
	{
		VertexShaderHandle vs = pos_only_vs;

		FragmentShaderHandle fs = create_shader_object(g_rm(),
													   app.shaders_dir / "build_depth_map.frag",
													   ShaderKind::FRAGMENT_SHADER,
													   app.shader_defs,
													   "@build_depth_map.frag")
									.get_value<FragmentShaderHandle>();

		app.shader_programs.build_depth_map =
		  link_shader_program(g_rm(), ShadersToUse::from_vs_fs(vs.rmid(), fs.rmid()));
	}

	// Program that simply blits the depth map to the screen
	{

		FragmentShaderHandle fs = create_shader_object(g_rm(),
													   app.shaders_dir / "blit_depth_map.frag",
													   ShaderKind::FRAGMENT_SHADER,
													   app.shader_defs,
													   "@blit_depth_map.frag")
									.get_value<FragmentShaderHandle>();

		app.shader_programs.blit_depth_map =
		  link_shader_program(g_rm(), ShadersToUse::from_vs_fs(pos_only_vs.rmid(), fs.rmid()));
	}

	// Program that renders the scene along with lights and shadows
	{
		VertexShaderHandle vs = create_shader_object(g_rm(),
													 app.shaders_dir / "with_shadow.vert",
													 ShaderKind::VERTEX_SHADER,
													 app.shader_defs,
													 "@with_shadow.vert")
								  .get_value<VertexShaderHandle>();

		FragmentShaderHandle fs =
		  create_shader_object(
			g_rm(), app.shaders_dir / "with_shadow_ct.frag", ShaderKind::FRAGMENT_SHADER, app.shader_defs)
			.get_value<FragmentShaderHandle>();

		app.shader_programs.with_shadow =
		  link_shader_program(g_rm(), ShadersToUse::from_vs_fs(vs.rmid(), fs.rmid()));

		// Sourcing the shadow transform
		app.shadow_related.scene_bs = app.scene_bounding_sphere;
		app.shadow_related.shadow_xform = shadow_map::clip_from_world_xform(app.shadow_map);
		source_to_uniform_buffer(g_rm(),
								 app.uniform_buffers.shadow_transform,
								 SourceToBufferInfo::after_discard(&app.shadow_related, 0));

#if 0
        LOG_F(INFO, "Shader info -- ");

        fo::ArenaAllocator aa(fo::memory_globals::default_allocator(), 5u << 20);
        auto aa_pmr = make_pmr_wrapper(aa);

        InspectedGLSL iglsl_with_shadow(app.shader_programs.with_shadow, aa_pmr);
        fo::string_stream::Buffer ss;
        iglsl_with_shadow.Print(ss);
        LOG_F(INFO, "Shader reflection info = \n%s", fo::string_stream::c_str(ss));

        write_file("with_shadow.iglsl.txt", (const u8 *)fo::string_stream::c_str(ss), fo::size(ss));
#endif
	}
}

const char *fs_quad_vs_source = R"(#version 430 core
    out VsOut {
        vec2 uv;
    } vsout;

    void main() {
        const uint id = 2 - gl_VertexID;
        gl_Position.x = float(id / 2) * 4.0 - 1.0;
        gl_Position.y = float(id % 2) * 4.0 - 1.0;
        gl_Position.z = -1.0;
        gl_Position.w = 1.0;

        vsout.uv.x = float(id / 2) * 2.0;
        vsout.uv.y = float(id % 2) * 2.0;
    }
)";

void load_tonemap_program(App &app)
{
	const auto defs_str = app.shader_defs.get_string();

	GLuint vs = create_shader_object(fs_quad_vs_source, ShaderKind::VERTEX_SHADER, app.shader_defs);

	const char *fs_src = R"(
    #version 430 core
    layout(binding = FP16_HDR_TEXTURE_BINDPOINT) uniform sampler2D fp16_texture_sampler;
    out vec4 fc;

    in VsOut {
        vec2 uv;
    } fs_in;

    layout(binding = TONEMAP_PARAMS_BINPOINT) uniform TonemapParams {
        uint u_already_ldr;
    };

    void main() {
        vec4 color = texture(fp16_texture_sampler, fs_in.uv);

        if (u_already_ldr != 1) {
            color.xyz = color.xyz * 1.5f / (color.xyz + 1.0);
            // fc = vec4(1.0);
        }
        fc = vec4(color.xyz, 1.0);
    }
    )";

	GLuint fs = create_shader_object(fs_src, ShaderKind::FRAGMENT_SHADER, app.shader_defs);

	app.shader_programs.basic_tonemap = create_program(vs, fs);
}

void load_png_blit_program(App &app)
{
	GLuint vs = create_shader_object(fs_quad_vs_source, ShaderKind::VERTEX_SHADER, app.shader_defs);

	const char *fs_src = R"(#version 430 core
layout(binding = STRUCTURED_TEXTURE_BINDING) uniform sampler2D s;

in VsOut {{
    vec2 uv;

}} fs_in;

out vec4 fc;

void main() {{
    fc = texture(s, gl_FragCoord.xy / vec2({}, {}));
}};

    )";

	auto fs_src_formatted = fmt::format(fs_src, constants::window_width, constants::window_height);

	GLuint fs = create_shader_object(fs_src_formatted.c_str(), ShaderKind::FRAGMENT_SHADER, app.shader_defs);

	app.shader_programs.png_blit = create_program(vs, fs);
}

void set_up_camera(App &app)
{
	const float distance = 20.0f;
	const float y_extent = 15.0f;

	app.camera.set_proj(0.1f,
						1000.0f,
						2.0f * std::atan(0.5f * y_extent / distance),
						// 70.0f * one_deg_in_rad,
						constants::window_height / float(constants::window_width));

	app.camera._eye = eye::toward_negz(10.0f);
	app.camera.update_view_transform();

	// Only need to store the camera's projection once
	app.eye_block.proj = app.camera.proj_xform();

	// The quad blitting eye block also needs to be initialized. Using identity matrices for these two.
	// The ortho projection will be done via the world transform.
	app.eye_block_for_blitting.view = identity_matrix;
	app.eye_block_for_blitting.proj = identity_matrix;
}

// ------------
//
// Uniform data initialization
// ------------
void init_uniform_data(App &app)
{
	// EyeBlock

	app.eye_block.view = app.camera.view_xform();
	app.eye_block.proj = app.camera.proj_xform();
	app.eye_block.camera_position = fo::Vector4(app.camera.position(), 1.0f);

	source_to_uniform_buffer(
	  g_rm(), app.uniform_buffers.camera_ubo, SourceToBufferInfo::after_discard(&app.eye_block, 0));

	source_to_uniform_buffer(
	  g_rm(),
	  app.uniform_buffers.dir_lights_list,
	  SourceToBufferInfo::after_discard(g_dir_lights.data(), 0, vec_bytes(g_dir_lights)));

	CHECK_NE_F(vec_bytes(g_dir_lights), 0);
}

// ---------------
//
// Send the uniform data for the object to the uniform buffer
// ---------------
void source_per_object_uniforms(App &app, const RenderableData &rd)
{
	source_to_uniform_buffer(g_rm(),
							 app.uniform_buffers.per_object_transforms,
							 SourceToBufferInfo::after_discard(&rd.per_object_uniform_data, 0));
}

// ---------------
//
// Source the given eye block uniform to the uniform buffer
// ---------------
void source_eye_block_uniform(App &app, CameraTransformUB &eye_block)
{
	source_to_uniform_buffer(
	  g_rm(), app.uniform_buffers.camera_ubo, SourceToBufferInfo::after_discard(&eye_block, 0));
}

// ---- Input handlers

class BasicInputHandler : public input::InputHandlerBase<App>
{
  private:
	struct {
		double x, y;
	} _prev_mouse_pos;

  public:
	BasicInputHandler(App &app)
	{
		glfwGetCursorPos(eng::gl().window, &_prev_mouse_pos.x, &_prev_mouse_pos.y);
	}

	void handle_on_key(App &app, input::OnKeyArgs args) override;

	void handle_on_mouse_move(App &app, input::OnMouseMoveArgs args) override;

	void handle_on_mouse_button(App &, input::OnMouseButtonArgs args) override;
};

void BasicInputHandler::handle_on_key(App &app, input::OnKeyArgs args)
{
	if (args.key == GLFW_KEY_ESCAPE && args.action == GLFW_PRESS) {
		app.window_should_close = true;
	}

	if (args.key == GLFW_KEY_B && args.action == GLFW_PRESS) {
		g_blur_style = BlurKind((g_blur_style + 1) % BLUR_STYLE_COUNT);
		bool blur_style_changed = true;

		if (blur_style_changed) {
			if (g_blur_style == DO_NO_BLUR) {
				app.do_blur = false;
				LOG_F(INFO, "No blur");
			} else if (g_blur_style == DO_BLUR_1 || g_blur_style == DO_BLUR_2) {
				toggle_blur_technique(app.pp_gaussian_blur);
				app.do_blur = true;
			}
		}
	}

	if (args.key == GLFW_KEY_SPACE && args.action == GLFW_PRESS) {
		// Toggle render full-screen texture
		app.do_render_png_blit = !app.do_render_png_blit;
	}
}

void BasicInputHandler::handle_on_mouse_move(App &app, input::OnMouseMoveArgs args)
{
	int state = glfwGetMouseButton(eng::gl().window, GLFW_MOUSE_BUTTON_RIGHT);

	if (state == GLFW_PRESS) {
		float diff = (float)(args.ypos - _prev_mouse_pos.y);
		float angle = -0.25f * one_deg_in_rad * diff;
		// app.camera.pitch(angle);

		diff = (float)(args.xpos - _prev_mouse_pos.x);
		angle = -0.25f * one_deg_in_rad * diff;
		app.camera.yaw(angle);
	}
	_prev_mouse_pos = { args.xpos, args.ypos };
}

void BasicInputHandler::handle_on_mouse_button(App &app, input::OnMouseButtonArgs args)
{
	if (args.button == GLFW_MOUSE_BUTTON_RIGHT && args.action == GLFW_PRESS) {
		double xpos, ypos;
		glfwGetCursorPos(eng::gl().window, &xpos, &ypos);
		_prev_mouse_pos = { xpos, ypos };
	}
}

DEFINE_GLFW_CALLBACKS(App);

void read_bindpoints_from_programs(App &app)
{
	// Get the bindpoint of resources from shader.

	LOCAL_FUNC read_bindpoints_for_program =
	  [&](BindpointsInProgram &bindpoints_out) {
		  InspectedGLSL glsl(eng::get_gluint_from_rmid(g_rm(), app.shader_programs.without_shadow.rmid()),
							 eng::pmr_default_resource());

		  // CONT: Use glGetUniformiv to get the texture unit the sampler is bound to, as specified in GLSL.
		  const auto &uniform_blocks = glsl.GetUniformBlocks();

		  for (const auto &block : uniform_blocks) {
			  bindpoints_out.uniform_blocks[block.name] = (GLuint)block.bufferBinding;
		  }

		  for (const auto &uniform : uniform_blocks) {
		  }
	  }

	LOG_F(INFO, "Exiting program as expected...");
	exit(EXIT_SUCCESS);

#if 0
	app.shader_defs.add("PER_OBJECT_UBLOCK_BINDING", (int)app.bound_ubos.per_object.binding);
	app.shader_defs.add("CAMERA_ETC_UBLOCK_BINDING", (int)app.bound_ubos.eye_block.binding);
	app.shader_defs.add("DIR_LIGHTS_LIST_UBLOCK_BINDING", (int)app.bound_ubos.dir_lights_list.binding);
	app.shader_defs.add("FLAT_COLOR", 1);
	app.shader_defs.add("NUM_DIR_LIGHTS", (int)constants::light_count);
	app.shader_defs.add("DEPTH_TEXTURE_UNIT", (int)app.shadow_map.depth_texture_unit);
	app.shader_defs.add("STRUCTURED_TEXTURE_BINDING", (int)app.textures.debug_box_texture.binding);
	app.shader_defs.add("DEPTH_TEXTURE_SIZE", (int)app.shadow_map.texture_size);
	app.shader_defs.add("FACE_DEPTH_DIFF_BINDING", (int)app.depth_texture.binding);
	app.shader_defs.add("SHADOW_RELATED_PARAMS_BINDING", (int)app.bound_ubos.shadow_related.binding);
	app.shader_defs.add("FP16_HDR_TEXTURE_BINDPOINT", (int)app.fp16_texture_bindpoint);
	app.shader_defs.add("TONEMAP_PARAMS_BINPOINT", (int)app.bound_ubos.tonemap_params.binding);
#endif
}

// -----
//
// Creates the the vertex buffers for each object, and a bounding sphere for the full scene.
// -----
void build_geometry_buffers_and_bounding_sphere(App &app)
{
	const auto build_buffer = [&](const mesh::MeshData &mesh_data, const char *shape_name) {
		const auto vbo_name = fmt::format("@vbo_{}", shape_name);
		const auto ebo_name = fmt::format("@ebo_{}", shape_name);

		BufferCreateInfo buffer_info;
		buffer_info.bytes = mesh_data.o.get_vertices_size_in_bytes();
		buffer_info.flags = BufferCreateBitFlags::SET_STATIC_STORAGE;
		buffer_info.init_data = (void *)mesh_data.buffer;
		buffer_info.name = vbo_name.c_str();

		VertexBufferHandle vbo_handle = create_vertex_buffer(g_rm(), buffer_info);

		buffer_info.bytes = mesh_data.o.get_indices_size_in_bytes();
		buffer_info.flags = BufferCreateBitFlags::SET_STATIC_STORAGE;
		buffer_info.init_data = (void *)(mesh_data.buffer + mesh_data.get_indices_offset_in_bytes());
		buffer_info.name = ebo_name.c_str();

		IndexBufferHandle ebo_handle = create_index_buffer(g_rm(), buffer_info);

		return std::make_pair(vbo_handle, ebo_handle);
	};

	mesh::Model unit_cube_model;
	eng::load_cube_mesh(unit_cube_model, math::identity_matrix, true, true);
	auto p = build_buffer(unit_cube_model[0]);
	app.vbos.cube = p.first;
	app.ebos.cube = p.second;
	app.stripped_meshes.cube = unit_cube_model[0];

	mesh::Model unit_sphere_model;
	eng::load_sphere_mesh(unit_sphere_model, 30, 30);
	p = build_buffer(unit_sphere_model[0]);
	app.vbos.sphere = p.first;
	app.ebos.sphere = p.second;
	app.stripped_meshes.sphere = unit_sphere_model[0];

	app.vao_pos = eng::gen_vao({ eng::VaoFloatFormat(0, 3, GL_FLOAT, GL_FALSE, 0) });

	// Positions of all vertices in the scene
	Array<Vector3> all_positions(memory_globals::default_allocator());
	reserve(all_positions, 512);

	for (const auto &object : g_scene_objects) {
		app.opaque_renderables.push_back(RenderableData{});
		auto &rd = app.opaque_renderables.back();

		VT_SWITCH(object.shape)
		{
			VT_CASE(object.shape, ShapeSphere)
				:
			{
				const auto &sphere = get_value<ShapeSphere>(object.shape);
				rd.uniforms.world_from_local_xform =
				  xyz_scale_matrix(sphere.radius, sphere.radius, sphere.radius);
				translate_update(rd.uniforms.world_from_local_xform, sphere.center);
				rd.vao = app.vao_pos_normal_st;
				rd.uniforms.inv_world_from_local_xform = inverse(rd.uniforms.world_from_local_xform);
				rd.packed_attr_size = app.stripped_meshes.sphere.packed_attr_size;
				rd.num_indices = app.stripped_meshes.sphere.num_faces * 3;
				rd.vbo = app.vbos.sphere;
				rd.ebo = app.ebos.sphere;

				std::vector<Vector3> positions;
				positions.reserve(app.stripped_meshes.sphere.num_vertices);

				std::transform(
				  mesh::positions_begin(unit_sphere_model[0]),
				  mesh::positions_end(unit_sphere_model[0]),
				  std::back_inserter(positions),
				  [&world_from_local_xform = rd.uniforms.world_from_local_xform](const Vector3 &point) {
					  return transform_point(world_from_local_xform, point);
				  });

				for (const auto &p : positions) {
					push_back(all_positions, p);
				}
			}
			break;

			VT_CASE(object.shape, ShapeCube)
				:
			{
				const auto &cube = get_value<ShapeCube>(object.shape);
				rd.uniforms.world_from_local_xform = xyz_scale_matrix(cube.half_extent * 2.0f);
				translate_update(rd.uniforms.world_from_local_xform, cube.center);
				rd.vao = app.vao_pos_normal_st;
				rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
				rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
				rd.uniforms.inv_world_from_local_xform = inverse(rd.uniforms.world_from_local_xform);
				rd.vbo = app.vbos.cube;
				rd.ebo = app.ebos.cube;

				std::vector<Vector3> positions;
				positions.reserve(app.stripped_meshes.cube.num_vertices);

				std::transform(
				  mesh::positions_begin(unit_cube_model[0]),
				  mesh::positions_end(unit_cube_model[0]),
				  std::back_inserter(positions),
				  [&world_from_local_xform = rd.uniforms.world_from_local_xform](const Vector3 &point) {
					  return transform_point(world_from_local_xform, point);
				  });

				for (const auto &p : positions) {
					push_back(all_positions, p);
				}
			}
			break;

			VT_CASE(object.shape, ShapeModelPath)
				:
			{
				const auto &model_info = get_value<ShapeModelPath>(object.shape);
				rd.uniforms.world_from_local_xform = xyz_scale_matrix(model_info.scale);

				rd.uniforms.world_from_local_xform =
				  rd.uniforms.world_from_local_xform * rotation_matrix(unit_z, model_info.euler_xyz_m.z);
				rd.uniforms.world_from_local_xform =
				  rd.uniforms.world_from_local_xform * rotation_matrix(unit_y, model_info.euler_xyz_m.y);
				rd.uniforms.world_from_local_xform =
				  rd.uniforms.world_from_local_xform * rotation_matrix(unit_x, model_info.euler_xyz_m.x);

				rd.uniforms.inv_world_from_local_xform = inverse(rd.uniforms.world_from_local_xform);

				translate_update(rd.uniforms.world_from_local_xform, model_info.position);

				print_matrix_classic("Model's matrix", rd.uniforms.world_from_local_xform);

				mesh::Model model;
				CHECK_F(mesh::load(model,
								   model_info.path.u8string().c_str(),
								   Vector2{ 0.0f, 0.0f },
								   mesh::CALC_TANGENTS | mesh::TRIANGULATE | mesh::CALC_NORMALS |
									 mesh::FILL_CONST_UV));
				// CHECK_EQ_F(size(model._mesh_array), 1);
				auto &mesh_data = model[0];

				glGenBuffers(1, &rd.vbo);
				glGenBuffers(1, &rd.ebo);
				glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
				glBufferData(
				  GL_ARRAY_BUFFER, vertex_buffer_size(mesh_data), mesh_data.buffer, GL_STATIC_DRAW);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
				glBufferData(
				  GL_ELEMENT_ARRAY_BUFFER, index_buffer_size(mesh_data), indices(mesh_data), GL_STATIC_DRAW);

				CHECK_NE_F(mesh_data.normal_offset, mesh::ATTRIBUTE_NOT_PRESENT);
				CHECK_NE_F(mesh_data.tex2d_offset, mesh::ATTRIBUTE_NOT_PRESENT);

				rd.packed_attr_size = mesh_data.packed_attr_size;
				rd.vao = app.vao_pos_normal_st;
				rd.num_indices = num_indices(mesh_data);
			}
			break;

		default:
			assert(false && "Not implemented for this object");
		}
	}

	// Calculate the bounding sphere encompassing all the renderable objects
	{
		PrincipalAxis pa = calculate_principal_axis(data(all_positions), size(all_positions));
		app.scene_bounding_sphere =
		  create_bounding_sphere_iterative(pa, data(all_positions), size(all_positions));

		auto &rd = app.rd_scene_bs;
		rd.uniforms.world_from_local_xform = translation_matrix(app.scene_bounding_sphere.center) *
		  uniform_scale_matrix(app.scene_bounding_sphere.radius);
		rd.uniforms.inv_world_from_local_xform = inverse(rd.uniforms.world_from_local_xform);
		rd.uniforms.material = GIZMO_MATERIAL;
		rd.vao = app.vao_pos_normal_st;
		rd.packed_attr_size = app.stripped_meshes.sphere.packed_attr_size;
		rd.num_indices = app.stripped_meshes.sphere.num_faces * 3;
		rd.vbo = app.vbos.sphere;
		rd.ebo = app.ebos.sphere;
	}

	// Build the sphere xyz lines
	{
		auto &rd = app.rd_sphere_axes;
		glGenBuffers(1, &rd.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
		rd.vao = app.vao_pos;
		rd.uniforms.material = GIZMO_MATERIAL;

		Array<Vector3> line_points({ -unit_x, unit_x, -unit_y, unit_y, -unit_z, unit_z },
								   memory_globals::default_allocator());

		glBufferData(GL_ARRAY_BUFFER, vec_bytes(line_points), data(line_points), GL_STATIC_DRAW);

		rd.uniforms.world_from_local_xform = translation_matrix(app.scene_bounding_sphere.center) *
		  uniform_scale_matrix(app.scene_bounding_sphere.radius);

		rd.packed_attr_size = sizeof(Vector3);
	}

	// Initialize the screen quad renderable
	{
		const f32 w = (f32)constants::window_width;
		const f32 h = (f32)constants::window_height;
		Array<Vector3> quad_corners({ Vector3{ 0.f, 0.f, 0.f },
									  Vector3{ w, 0.f, 0.f },
									  Vector3{ 0.f, h, 0.f },
									  Vector3{ w, 0.f, 0.f },
									  Vector3{ w, h, 0.f },
									  Vector3{ 0.f, h, 0.f } });

		glGenBuffers(1, &app.rd_screen_quad.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, app.rd_screen_quad.vbo);
		glBufferData(GL_ARRAY_BUFFER, vec_bytes(quad_corners), data(quad_corners), GL_STATIC_DRAW);
		app.rd_screen_quad.vao = app.vao_pos;
		app.rd_screen_quad.packed_attr_size = sizeof(Vector3);
		app.rd_screen_quad.uniforms.world_from_local_xform =
		  orthographic_projection(0.0f, 0.0f, (f32)constants::window_width, (f32)constants::window_height);
	}
}

void set_up_materials(App &app)
{
	// CHECK_EQ_F(app.opaque_renderables.size(), g_scene_objects.size());
	for (u32 i = 0; i < g_scene_objects.size(); ++i) {
		auto &object = g_scene_objects[i];

		switch (type_index(object.shape)) {
		case 0: {
			app.opaque_renderables[i].uniforms.material = optional_value(object.material, BALL_MATERIAL);
		} break;
		case 1: {
			app.opaque_renderables[i].uniforms.material = optional_value(object.material, FLOOR_MATERIAL);
		} break;
		case 2: {
			app.opaque_renderables[i].uniforms.material = optional_value(object.material, FLOOR_MATERIAL);
		} break;
		default:
			CHECK_F(false);
		}
	}

	LOG_F(INFO, "Set up materials");
}

// ---------
//
// Put some lights into the g_dir_lights list
// ---------
void set_up_lights(App &app)
{
	// Push some lights into the global lights list. The upper 4 quadrants of the bounding sphere each
	// have a light, while the second quadrant contains the light that will cast shadows
	Vector3 center = app.scene_bounding_sphere.center;

	std::vector<Vector3> position_store;
	position_store.reserve(4);

	// First quadrant
	f32 radius = app.scene_bounding_sphere.radius + 20.0f;
	f32 ang_y = 70.0f * one_deg_in_rad;
	f32 ang_zx = 45.0f * one_deg_in_rad;

	Vector3 pos = center +
	  Vector3{ radius * std::sin(ang_y) * std::sin(ang_zx),
			   radius * std::cos(ang_y),
			   radius * std::sin(ang_y) * std::cos(ang_zx) };

	// A convenient light direction for the purpose of this demo is to point at the center of the bounding
	// sphere. Then x_max = r, and x_min = -r. Same for y_max and y_min.

	// Vector3 pos = center + Vector3{radius, radius, radius};
	g_dir_lights.emplace_back(
	  pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.4f, 0.4f, 0.4f), Vector3(0.0f, 0.0f, 0.0f));

	position_store.push_back(pos);

	// Second quadrant
	Matrix4x4 rotation = rotation_about_y(pi / 2.0f);
	pos = Vector3(rotation * Vector4(pos, 1.0f));
	g_dir_lights.emplace_back(
	  pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.9f, 0.9f, 0.9f), Vector3(0.0f, 0.0f, 0.0f));

	position_store.push_back(pos);

	// Third quadrant
	rotation_about_y_update(rotation, 2 * pi / 2.0f);
	pos = Vector3(rotation * Vector4(pos, 1.0f));
	g_dir_lights.emplace_back(
	  pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.2f, 0.2f, 0.2f), Vector3(0.0f, 0.0f, 0.0f));

	position_store.push_back(pos);

	// Fourth quadrant
	rotation_about_y_update(rotation, 3 * pi / 2.0f);
	pos = Vector3(rotation * Vector4(pos, 1.0f));
	g_dir_lights.emplace_back(
	  pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.2f, 0.2f, 0.2f), Vector3(0.0f, 0.0f, 0.0f));

	position_store.push_back(pos);

	CHECK_F(constants::light_count == g_dir_lights.size(),
			"constants::light_count = %u, g_dir_lights.size() = %zu",
			constants::light_count,
			g_dir_lights.size());

	// For each light, insert a cube renderable

	for (size_t i = 0; i < g_dir_lights.size(); ++i) {
		const auto &l = g_dir_lights[i];

		auto &rd = push_back_get(app.opaque_renderables, RenderableData{});
		rd.vbo = app.vbos.cube;
		rd.vao = app.vao_pos_normal_st;
		rd.ebo = app.ebos.cube;
		rd.uniforms.world_from_local_xform = translation_matrix(position_store[i]);
		rd.uniforms.inv_world_from_local_xform = translation_matrix(-position_store[i]);
		rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
		rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
		rd.uniforms.material = LIGHT_GIZMO_MATERIAL;
	}

	LOG_F(INFO, "Added lights to scene");
}

void set_up_casting_light_gizmo(App &app)
{
	mesh::Model m(memory_globals::default_allocator(), memory_globals::default_allocator());
	load_dir_light_mesh(m);

	GLuint vbo;
	GLuint ebo;
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, mesh::vertex_buffer_size(m[0]), vertices(m[0]), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh::index_buffer_size(m[0]), indices(m[0]), GL_STATIC_DRAW);

	auto &rd = app.rd_casting_light;

	rd.vbo = vbo;
	rd.ebo = ebo;
	rd.vao = app.vao_pos;
	rd.packed_attr_size = m[0].packed_attr_size;
	rd.num_indices = mesh::num_indices(m[0]);
	rd.uniforms.material = PINK_MATERIAL;
	rd.uniforms.world_from_local_xform = inverse_rotation_translation(light_from_world_xform(app.shadow_map));
}

void init_rasterizer_states(App &app)
{
	RasterizerStateDesc rs = default_rasterizer_state_desc;
	app.rasterizer_states.without_shadows = eng::gl().bs.add_rasterizer_state(rs);

	rs = default_rasterizer_state_desc;
	// rs.slope_scaled_depth_bias = 3.0f;
	// rs.constant_depth_bias = 4;
	app.rasterizer_states.first_pass = eng::gl().bs.add_rasterizer_state(rs);

	rs = default_rasterizer_state_desc;
	app.rasterizer_states.second_pass = eng::gl().bs.add_rasterizer_state(rs);
}

void init_shadow_map(App &app)
{
	shadow_map::InitInfo init_info;
	init_info.light_direction = g_dir_lights[0].direction;
	init_info.light_position = g_dir_lights[0].position;
	init_info.texture_size = 2048;

	// Like I said, x_max = r, x_min = -r. So extent = 2r. No translation along x needed either.
	init_info.x_extent = app.scene_bounding_sphere.radius * 2.0f;
	init_info.y_extent = app.scene_bounding_sphere.radius * 2.0f;
	init_info.neg_z_extent = magnitude(app.scene_bounding_sphere.center - g_dir_lights[0].position) +
	  app.scene_bounding_sphere.radius;
	shadow_map::init(app.shadow_map, init_info);

	Matrix4x4 world_from_light_xform =
	  inverse_rotation_translation(app.shadow_map.eye_block.view_from_world_xform);

	// Create a vbo for the lines to denote the light's local axes
	{
		glGenBuffers(1, &app.rd_light_xyz.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, app.rd_light_xyz.vbo);
		Array<Vector3> line_points(memory_globals::default_allocator());
		resize(line_points, 6);
		line_points[0] = -unit_x;
		line_points[1] = unit_x;
		line_points[2] = -unit_y;
		line_points[3] = unit_y;
		line_points[4] = zero_3;
		line_points[5] = unit_z;
		glBufferData(GL_ARRAY_BUFFER, vec_bytes(line_points), data(line_points), GL_STATIC_DRAW);
		app.rd_light_xyz.vao = app.vao_pos;
		app.rd_light_xyz.ebo = 0;

		app.rd_light_xyz.uniforms.world_from_local_xform = world_from_light_xform *
		  xyz_scale_matrix(init_info.x_extent / 2.0f, init_info.y_extent / 2.0f, -init_info.neg_z_extent);
		app.rd_light_xyz.uniforms.material = BALL_MATERIAL;
		app.rd_light_xyz.packed_attr_size = sizeof(Vector3);
	}
	// Create the renderable for the light bounding box
	{
		auto &rd = app.rd_light_box;
		rd.vao = app.vao_pos_normal_st;
		rd.vbo = app.vbos.cube;
		rd.ebo = app.ebos.cube;

		rd.uniforms.world_from_local_xform =
		  xyz_scale_matrix(Vector3{ init_info.x_extent, init_info.y_extent, init_info.neg_z_extent });
		translate_update(rd.uniforms.world_from_local_xform, { 0.0f, 0.0f, -init_info.neg_z_extent * 0.5f });
		rd.uniforms.world_from_local_xform = world_from_light_xform * rd.uniforms.world_from_local_xform;

		rd.uniforms.inv_world_from_local_xform = inverse(rd.uniforms.world_from_local_xform);
		rd.uniforms.material = PINK_MATERIAL;
		rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
		rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
	}
}

// -----
//
// Renders to the depth map from the point of view of the casting light
// -----
void render_to_depth_map(App &app)
{
	begin_timer(app.timer_manager, "build_depth_map");
	DEFERSTAT(end_timer(app.timer_manager, "build_depth_map"));

	glViewport(0, 0, app.shadow_map.texture_size, app.shadow_map.texture_size);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_BLEND);

	eng::gl().bs.set_rasterizer_state(app.rasterizer_states.first_pass);

	// Set the depth buffer as current render target, and clear it.
	shadow_map::set_as_draw_fbo(app.shadow_map, app.hdr_color_fbo.fbo());
	shadow_map::clear(app.shadow_map);

	// Set the eye transforms
	source_eye_block_uniform(app, app.shadow_map.eye_block);

	glUseProgram(app.shader_programs.build_depth_map);

	// Render all opaque objects
#if 0
    {
        glBindVertexArray(app.opaque_renderables[0].vao);

    for (const RenderableData &rd : app.opaque_renderables) {
        source_per_object_uniforms(app, rd);
        // glBindVertexArray(rd.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
        glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
        glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
    }
    }
#else

	glBindVertexArray(app.opaque_renderables[0].vao);
	for (const auto range : app.shape_ranges) {
		auto &rd0 = app.opaque_renderables[range.first];
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd0.ebo);
		glBindVertexBuffer(0, rd0.vbo, 0, rd0.packed_attr_size);
		for (size_t i = range.first; i < range.second; ++i) {
			auto &rd = app.opaque_renderables[i];
			source_per_object_uniforms(app, rd);
			glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
		}
	}

#endif

	// Restore hdr framebuffer
	shadow_map::set_as_read_fbo(app.shadow_map, app.hdr_color_fbo.fbo());
}

// -----------
//
// Visualize the depth map by blitting it to the screen
// -----------
void blit_depth_map_to_screen(App &app)
{
	glViewport(0, 0, constants::window_width, constants::window_height);

	glUseProgram(app.shader_programs.blit_depth_map);
	glDisable(GL_DEPTH_TEST);

	eng::gl().bs.set_rasterizer_state(app.rasterizer_states.first_pass);

	shadow_map::unbind_comparing_sampler(app.shadow_map);

	// Use an orthographic projection to blit the quad to the screen
	source_eye_block_uniform(app, app.eye_block_for_blitting);

	// Draw the screen quad
	auto &rd = app.rd_screen_quad;
	source_per_object_uniforms(app, rd);
	glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
	glBindVertexArray(rd.vao);
	glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void render_second_pass(App &app)
{
	begin_timer(app.timer_manager, "second_pass");
	DEFERSTAT(end_timer(app.timer_manager, "second_pass"));

	const Vector4 clear_color = colors::SpringGreen;

	app.hdr_color_fbo.fbo().bind_as_writable();
	app.hdr_color_fbo.clear_color(clear_color);
	app.hdr_color_fbo.clear_depth(1.0f);

	eng::gl().bs.set_rasterizer_state(app.rasterizer_states.first_pass);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_BLEND);
	glViewport(0, 0, constants::window_width, constants::window_height);

	// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	shadow_map::bind_comparing_sampler(app.shadow_map);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	// Source the main camera's eye block.
	glInvalidateBufferData(app.bound_ubos.eye_block.handle());
	source_eye_block_uniform(app, app.eye_block);

	{
		// Only difference with render_without_shadow is that we use the other shader program. Factor this
		// out.
		glUseProgram(app.shader_programs.with_shadow);

#if 0

    glBindVertexArray(app.opaque_renderables[0].vao);

    for (const RenderableData &rd : app.opaque_renderables) {
        source_per_object_uniforms(app, rd);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
        glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
        glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
    }
#else

		glBindVertexArray(app.opaque_renderables[0].vao);
		for (const auto range : app.shape_ranges) {
			auto &rd0 = app.opaque_renderables[range.first];
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd0.ebo);
			glBindVertexBuffer(0, rd0.vbo, 0, rd0.packed_attr_size);
			for (size_t i = range.first; i < range.second; ++i) {
				auto &rd = app.opaque_renderables[i];
				source_per_object_uniforms(app, rd);
				glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
			}
		}

#endif
	}
}

void render_post_blur(App &app) { render_pp_gaussian_blur(app.pp_gaussian_blur); }

void tonemap_to_backbuffer(App &app)
{
	app.hdr_color_fbo.fbo().bind_as_readable(GLuint(app.screen_fbo));

	begin_timer(app.timer_manager, "tonemap_pass");
	DEFERSTAT(end_timer(app.timer_manager, "tonemap_pass"));

	glDisable(GL_DEPTH_TEST);
	glUseProgram(app.shader_programs.basic_tonemap);

	glBindTextureUnit(app.fp16_texture_bindpoint, app.hdr_color_fbo.fbo()._color_buffer_textures[0]);

	{
		TonemapParamsUB p;
		p.already_ldr = app.do_render_png_blit ? 1u : 0u;
		glBindBuffer(GL_UNIFORM_BUFFER, app.bound_ubos.tonemap_params.handle());
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(p), &p);
	}

	eng::draw_full_screen_quad();
}

void render_with_shadow(App &app)
{
	static bool captured_once = false;
	{
		render_to_depth_map(app);
		render_second_pass(app);
		if (!captured_once) {
			eng::trigger_renderdoc_frame_capture(1);
		}
	}

	if (app.do_blur) {
		render_post_blur(app);
		if (!captured_once) {
			eng::trigger_renderdoc_frame_capture(1);
			captured_once = true;
		}
	}

	tonemap_to_backbuffer(app);
}

void render_png_blit(App &app)
{
	// app.hdr_color_fbo.fbo().bind_as_writable();

	app.hdr_color_fbo.fbo().bind_as_writable();

	glUseProgram(app.shader_programs.png_blit);
	glBindTextureUnit(app.textures.debug_box_texture.binding, app.textures.debug_box_texture.handle());
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Blur
	if (app.do_blur) {
		render_pp_gaussian_blur(app.pp_gaussian_blur);
	}

	tonemap_to_backbuffer(app);
}

void sort_shape_vbos(App &app)
{
	std::sort(app.opaque_renderables.begin(),
			  app.opaque_renderables.end(),
			  [](const auto &rd1, const auto &rd2) { return rd1.vbo < rd2.vbo; });

	size_t start = 0;
	size_t i = start + 1;

	do {
		while (i < app.opaque_renderables.size() &&
			   app.opaque_renderables[i - 1].vbo == app.opaque_renderables[i].vbo) {
			i++;
		}
		app.shape_ranges.push_back({ start, i });
		start = i;
		i++;
	} while (i < app.opaque_renderables.size());
}

namespace app_loop
{

	static eng::StartGLParams glparams;

	template <> void init(App &app)
	{
		auto t0 = std::chrono::high_resolution_clock::now();

		// ImGui::CreateContext();
#if 1
		// imglfw_init(eng::gl().window);
		// imgl3_init();
#else

		ImGui_ImplGlfw_InitForOpenGL(eng::gl().window, true);
		ImGui_ImplOpenGL3_Init();

#endif

		app.set_input_handler(input::make_handler<BasicInputHandler>(app));
		app.hdr_color_fbo.create(FBOCreateConfig{
		  constants::window_width,
		  constants::window_height,
		  1,
		  true,
		});
		set_fbo_label(app.hdr_color_fbo.fbo()._fbo_handle, "gl@hdr_color_fbo");
		app.fp16_texture_bindpoint = eng::gl().bs.reserve_sampler_bindpoint();
		app.screen_fbo.init_from_default_framebuffer();

		init_pp_gaussian_blur(app.pp_gaussian_blur, 9, &app.hdr_color_fbo);

		REGISTER_GLFW_CALLBACKS(app, eng::gl().window);

		build_geometry_buffers_and_bounding_sphere(app);
		load_debug_box_texture(app);
		set_up_lights(app);
		set_up_materials(app);
		create_uniform_buffers(app);
		init_shadow_map(app);
		init_uniform_data(app);
		set_up_camera(app);
		set_up_casting_light_gizmo(app);

		sort_shape_vbos(app);

		init_rasterizer_states(app);

		LOG_F(INFO, "Shader defines =\n%s", app.shader_defs.get_string().c_str());

		load_shadow_map_programs(app);
		load_tonemap_program(app);
		load_png_blit_program(app);

		read_bindpoints_from_programs(app);

		app.camera.move_upward(10.0f);
		app.camera.move_forward(-20.0f);

		auto t1 = std::chrono::high_resolution_clock::now();

		std::chrono::duration<double, std::ratio<1, 1000>> startup_time(t1 - t0);

		LOG_F(INFO, "Took %.2f ms to render first frame", startup_time.count());

		LOG_F(INFO, "Number of opaque renderables = %zu", g_scene_objects.size());

		eng::gl_timer_query::add_timer(app.timer_manager, "without_shadow");
		eng::gl_timer_query::add_timer(app.timer_manager, "with_shadow");
		eng::gl_timer_query::add_timer(app.timer_manager, "swap_buffer");
		eng::gl_timer_query::add_timer(app.timer_manager, "build_depth_map");
		eng::gl_timer_query::add_timer(app.timer_manager, "second_pass");
		eng::gl_timer_query::add_timer(app.timer_manager, "tonemap_pass");
		// eng::gl_timer_query::set_no_warning(app.timer_manager);
		eng::gl_timer_query::done_adding(app.timer_manager);

		eng::gl_timer_query::set_disabled(app.timer_manager, true);
	}

	template <> void update(App &app, State &state)
	{
		glfwPollEvents();

#if 1

		// imglfw_new_frame();
		// imgl3_new_frame();

#else

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();

#endif

		// ImGui::NewFrame();

		static bool s = true;

		if (s) {
			// ImGui::ShowDemoWindow(&s);
		}

		time_in_sec += (float)state.frame_time_in_sec;

		if (app.camera.needs_update() ||
			eng::handle_eye_input(
			  eng::gl().window, app.camera._eye, state.frame_time_in_sec, app.camera._view_xform)) {

			app.camera.update_view_transform();
			update_camera_eye_block(app, state.frame_time_in_sec);
			app.camera.set_needs_update(false);
		}
	}

	template <> void render(App &app)
	{
		eng::gl_timer_query::new_frame(app.timer_manager);

		glDisable(GL_SCISSOR_TEST);

		if (!app.do_render_png_blit) {
			render_png_blit(app);
		} else {
			render_with_shadow(app);
		}

		// ImGui::Render();

#if 1
		// imgl3_render_draw_data(ImGui::GetDrawData());
#else

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#endif

		{
			eng::gl_timer_query::begin_timer(app.timer_manager, "swap_buffer");
			glfwSwapBuffers(eng::gl().window);
			eng::gl_timer_query::end_timer(app.timer_manager, "swap_buffer");
		}

		eng::gl_timer_query::end_frame(app.timer_manager);

	} // namespace app_loop

	template <> void close(App &app)
	{
		eng::gl_timer_query::wait_for_last_frame(app.timer_manager);

		string_stream::Buffer ss(memory_globals::default_allocator());
		eng::gl_timer_query::print_times(app.timer_manager, ss);
		LOG_F(INFO, "\n%s", string_stream::c_str(ss));

		// imgl3_shutdown();
		// imglfw_shutdown();
	}

	template <> bool should_close(App &app)
	{
		return glfwWindowShouldClose(eng::gl().window) || app.window_should_close;
	}

} // namespace app_loop

int main(int ac, char **av)
{
	init_memory();
	DEFERSTAT(shutdown_memory());

	eng::StartGLParams glparams;

	glparams.window_width = constants::window_width;
	glparams.window_height = constants::window_height;
	glparams.window_title = "shadow map playground";
	glparams.major_version = 4;
	glparams.minor_version = 5;
	glparams.load_renderdoc = false;
	glparams.debug_callback_severity.low = true;
	glparams.debug_callback_severity.notification = true;
	glparams.abort_on_error = true;
	glparams.mild_output_logfile = "/tmp/image_based_test.log";
	glparams.debug_callback_severity = { true, true, true, true };

	eng::start_gl(glparams, eng::gl());
	DEFERSTAT(eng::close_gl(glparams, eng::gl()));

	eng::add_shader_search_path(fs::path(SOURCE_DIR) / "data");

	{
		App app(eng::gl().string_table);

		app_loop::State loop_state{};
		app_loop::run(app, loop_state);
	}
}