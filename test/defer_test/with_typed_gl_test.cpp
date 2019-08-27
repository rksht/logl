// A copy of defer_light_test but testing my typed_gl_resources_abstraction.

#include <learnogl/app_loop.h>
#include <learnogl/eng>

#include <learnogl/bounding_shapes.h>
#include <learnogl/intersection_test.h>
#include <learnogl/nf_simple.h>
#include <learnogl/random_sampling.h>

#include <clara.hpp>

using namespace eng::math;

// The world bounds are specified in this record
struct SphereWorld {
	f32 radius_in_meters = 100;
	f32 pixels_per_meter = 40;
};

// Size, position, orientation of a single sphere.
struct SphereInstance {
	fo::Vector3 position;
	f32 radius;
	fo::Quaternion orientation; // Orientation's effect is only apparent with texturing.

	eng::BoundingSphere pos_radius() const { return { position, radius }; }
};

TU_LOCAL eng::StartGLParams glparams = [] {
	eng::StartGLParams glparams;
	glparams.window_width = 1366;
	glparams.window_height = 768;
	glparams.window_title = "With Typed GL Test (Deferred lighting)";

	return glparams;
}();

constexpr f32 PIXELS_PER_METER = 40;

TU_LOCAL fo::StringId64 gbuffer_record_pass("gbuffer_record_pass");
TU_LOCAL fo::StringId64 lighting_pass("lighting_pass");
TU_LOCAL fo::StringId64 debug_grid_pass("debug_grid_pass");
TU_LOCAL fo::StringId64 simple_forward_pass("simple_forward_pass");

struct App {
	fo::Vector<SphereInstance> sphere_instances;

	fo::Vector<fo::Vector3> point_light_positions;
	fo::Vector<fo::Vector3> point_light_intensties;
	fo::Vector<fo::Vector3> point_light_influence_distance;

	eng::VertexArrayHandle vao_handle;		// Usual pos-normal-uv-tangent vao
	eng::VertexBufferHandle sphere_mesh_vb; // Sphere mesh vertex buffer
	eng::IndexBufferHandle sphere_mesh_ib;  // Sphere mesh index buffer

	// Shaders and programs for the record pass
	eng::ShaderProgramHandle gbuffer_recorder_vsfs;
	eng::VertexShaderHandle gbuffer_recorder_vs;
	eng::FragmentShaderHandle gbuffer_recorder_fs;

	// Shaders and programs for the lighting pass
	eng::ShaderProgramHandle lighting_pass_vs;
	eng::ShaderProgramHandle lighting_pass_fs;

	// An ssbo for storing the attributes of each sphere.
	eng::ShaderStorageBufferHandle sphere_attributes_ssbo;

	struct RenderStates {
		eng::BlendFunctionDescId blend_func;
		eng::DepthStencilStateId depth_stencil;
		eng::RasterizerStateId raster;
	};

	fo::PodHash<fo::StringId64, RenderStates> render_state_for =
	  fo::make_pod_hash<fo::StringId64, RenderStates>(fo::memory_globals::default_allocator());
};

void init_gl_objects(App &a)
{
	// Create the gbuffer. Normal, uv, diffuse color.
	//

	// Normal
	eng::TextureCreateInfo normal_ci;
	normal_ci.width = glparams.window_width;
	normal_ci.height = glparams.window_height;
	normal_ci.texel_info.internal_type = eng::TexelOrigType::FLOAT16;
	normal_ci.texel_info.interpret_type = eng::TexelInterpretType::UNNORMALIZED;
	normal_ci.texel_info.components = eng::TexelComponents::RGB;

	eng::create_texture(g_rm(), normal_ci);
}

namespace app_loop
{
	template <> void init<App>(App &a) { init_gl_objects(a); }

	template <> void update<App>(App &app, State &state) {}

	template <> void render<App>(App &a) {}

	template <> bool should_close<App>(App &a) {}

	template <> void close<App>(App &a) {}

} // namespace app_loop

auto path_to_world_file(u32 num_spheres, u32 num_lights)
{
	auto file_name = fmt::format("spheres-{}-lights-{}.data", num_spheres, num_lights);
	auto path = fs::temp_directory_path() / file_name;
	return path;
}

struct GenerateSceneInfo {
	u32 num_spheres = 100;
	u32 num_lights = 8;
	u32 scene_radius = 100;
	fo::Vector3 scene_center = zero_3;
};

int main_generate_sphere_world(GenerateSceneInfo gen_scene_info)
{
	auto [num_spheres, num_lights, scene_radius, scene_center] = gen_scene_info;

	fo::Array<SphereInstance> spheres;
	fo::reserve(spheres, num_spheres);

	fo::Array<fo::Vector3> light_positions;
	fo::reserve(light_positions, num_lights);

	int max_times_tried = 0;

	for (u32 sphere_number = 0; sphere_number < num_spheres; ++sphere_number) {
		int times_tried = 0;
		while (times_tried < 100) {

			SphereInstance instance;
			instance.radius = rng::random(0.01f, scene_radius / 5.0f - 0.1);
			instance.position = scene_center + ::random_point_inside_sphere(scene_radius - instance.radius);

			bool overlapping = false;
			for (u32 prev = 0; prev < sphere_number; ++prev) {
				auto &p = spheres[prev];

				if (eng::test_sphere_sphere(p.position, p.radius, instance.position, instance.radius)) {
					overlapping = true;
					++times_tried;
					break;
				}
			}

			if (overlapping) {
				if (times_tried == 100) {
					ABORT_F(
					  "Tried too many times to generate non-intersecting sphere, but failed. Sphere count = "
					  "%u",
					  sphere_number);
				}
				continue;
			}

			// Set an orientation
			fo::Vector3 sphere_axis = ::random_unit_vector();
			const fo::Vector3 reference_axis = unit_y;
			instance.orientation = versor_from_axis_angle(normalize(cross(sphere_axis, reference_axis)),
														  std::acos(dot(sphere_axis, reference_axis)));

			fo::push_back(spheres, instance);

			max_times_tried = std::max(max_times_tried, times_tried);
			break;
		}
	}

	LOG_F(INFO, "Done creating spheres. Max times tried = %i", max_times_tried);

	// Create some point lights. Don't create inside a sphere. x)

	for (u32 i = 0; i < num_lights; ++i) {
		int times_tried = 0;

		while (times_tried < 100) {
			fo::Vector3 position = scene_center + ::random_point_inside_sphere(scene_radius);
			bool overlapping = false;
			for (auto &sphere : spheres) {
				auto diff = sphere.position - position;
				if (square_magnitude(diff) < sphere.radius * sphere.radius) {
					++times_tried;
					overlapping = true;
					break;
				}
			}

			if (overlapping) {
				if (times_tried == 100) {
					ABORT_F("Tried too many times to set a light position randomly");
				}

				continue;
			}

			fo::push_back(light_positions, position);
			break;
		}
	}

	LOG_F(INFO, "Done creating lights");

	// Make a config data and write to file
	{
		nfcd_ConfigData *cd = nfcd_make(simple_nf_realloc, nullptr, kilobytes(4), 0);

		fn_ make_vector = [](nfcd_ConfigData **cd, std::initializer_list<f32> floats) -> nfcd_loc {
			nfcd_loc array_loc = nfcd_add_array(cd, (int)floats.size());
			for (float f : floats) {
				nfcd_push(cd, array_loc, nfcd_add_number(cd, f));
			}
			return array_loc;
		};

		nfcd_loc root_loc = nfcd_add_object(&cd, 128);

		nfcd_loc scene_center_loc = make_vector(&cd, { scene_center.x, scene_center.y, scene_center.z });

		nfcd_set(&cd, root_loc, "scene_center", scene_center_loc);
		nfcd_set(&cd, root_loc, "scene_radius", nfcd_add_number(&cd, scene_radius));

		nfcd_loc sphere_instances_arr_loc = nfcd_add_array(&cd, (int)fo::size(spheres));

		nfcd_set(&cd, root_loc, "sphere_instances", sphere_instances_arr_loc);

		for (u32 i = 0; i < fo::size(spheres); ++i) {
			auto &instance = spheres[i];
			nfcd_loc instance_loc = nfcd_add_object(&cd, sizeof(SphereInstance));
			nfcd_set(&cd,
					 instance_loc,
					 "position",
					 make_vector(&cd, { instance.position.x, instance.position.y, instance.position.z }));

			nfcd_set(&cd, instance_loc, "radius", nfcd_add_number(&cd, instance.radius));

			nfcd_set(&cd,
					 instance_loc,
					 "orientation",
					 make_vector(&cd,
								 { instance.orientation.x,
								   instance.orientation.y,
								   instance.orientation.z,
								   instance.orientation.w }));

			nfcd_set(&cd, instance_loc, "radius", nfcd_add_number(&cd, instance.radius));

			nfcd_push(&cd, sphere_instances_arr_loc, instance_loc);

			LOG_F(INFO, "i = %u", i);
		}

		nfcd_loc light_positions_loc = nfcd_add_array(&cd, (int)fo::size(light_positions));
		nfcd_set(&cd, root_loc, "light_positions", light_positions_loc);

		for (u32 i = 0; i < fo::size(light_positions); ++i) {
			auto &p = light_positions[i];
			nfcd_push(&cd, light_positions_loc, make_vector(&cd, { p.x, p.y, p.z }));
		}

		nfcd_set_root(cd, root_loc);

		auto sc_str = stringify_nfcd(cd);

		// Write this to the file.
		fs::path file_path = path_to_world_file(num_spheres, num_lights);
		write_file(file_path, (const u8 *)fo::string_stream::c_str(sc_str), fo::size(sc_str));
	}

	return 0;
}

int main_render(int ac, char **av)
{

	u32 num_spheres = 0;
	u32 num_lights = 0;

	bool show_help = false;

	using namespace clara;

	auto cli = Opt(num_spheres, "num-spheres")["--num-spheres"]("Number of spheres in the scene") |
	  Opt(num_lights, "num-lights")["--num-lights"]("Number of lights in the scene") | Help(show_help);

	cli.parse(Args(ac, av));

	auto file_path = path_to_world_file(num_spheres, num_lights);
	if (!fs::exists(file_path)) {
		ABORT_F("File %s does not exist", file_path.u8string().c_str());
	}

	App app;

	// Read scene file
	{
		nfcd_ConfigData *cd = simple_parse_file(file_path.u8string().c_str(), true);
		nfcd_loc root = nfcd_root(cd);

		nfcd_loc scene_radius_loc = nfcd_object_lookup(cd, root, "scene_radius");
		f32 scene_radius = SimpleParse<f32>::parse(cd, scene_radius_loc);

		nfcd_loc scene_center_loc = nfcd_object_lookup(cd, root, "scene_center");
		fo::Vector3 scene_center = SimpleParse<fo::Vector3>::parse(cd, scene_center_loc);

		nfcd_loc spheres_array_loc = nfcd_object_lookup(cd, root, "sphere_instances");

		fo::Vector<SphereInstance> sphere_instances;
		u32 num_spheres = nfcd_array_size(cd, spheres_array_loc);
		fo::reserve(sphere_instances, num_spheres);

		for (u32 i = 0; i < num_spheres; ++i) {
			nfcd_loc sphere_instance_loc = nfcd_array_item(cd, spheres_array_loc, (int)i);

			nfcd_loc position_loc = nfcd_object_lookup(cd, sphere_instance_loc, "position");
			nfcd_loc orientation_loc = nfcd_object_lookup(cd, sphere_instance_loc, "orientation");
			nfcd_loc radius_loc = nfcd_object_lookup(cd, sphere_instance_loc, "radius");

			SphereInstance s = ::push_back_get(sphere_instances, {});
			s.position = SimpleParse<fo::Vector3>::parse(cd, position_loc);
			fo::Vector4 orientation = SimpleParse<fo::Vector4>::parse(cd, orientation_loc);
			s.orientation = { orientation.x, orientation.y, orientation.z, orientation.w };
			s.radius = SimpleParse<f32>::parse(cd, radius_loc);
		}

		fo::Vector<fo::Vector3> light_positions;

		nfcd_loc light_pos_array_loc = nfcd_object_lookup(cd, root, "light_positions");
		u32 num_lights = nfcd_array_size(cd, light_pos_array_loc);

		fo::reserve(light_positions, num_lights);

		for (u32 i = 0; i < num_lights; ++i) {
			nfcd_loc position_loc = nfcd_array_item(cd, light_pos_array_loc, (int)i);
			fo::push_back(light_positions, SimpleParse<fo::Vector3>::parse(cd, position_loc));
		}

		app.sphere_instances = std::move(sphere_instances);
		app.point_light_positions = std::move(light_positions);
	}

	app_loop::State loop_state = {};
	app_loop::run(app, loop_state);

	return 0;
}

int main_generate(int ac, char **av)
{

	using namespace clara;

	bool show_help = false;

	GenerateSceneInfo scene_gen_info;

	auto cli =
	  Opt(scene_gen_info.num_spheres, "num-spheres")["--num-spheres"]("Number of spheres in the scene") |
	  Opt(scene_gen_info.num_lights, "num-lights")["--num-lights"]("Number of lights in the scene") |
	  Help(show_help);

	cli.parse(Args(ac, av));

	return main_generate_sphere_world(scene_gen_info);
}

int main(int ac, char **av)
{
	eng::init_memory();
	DEFERSTAT(eng::shutdown_memory());

	rng::init_rng(SCAFFOLD_SEED);

#if defined(WITH_TYPED_TEST_GENERATE)
	return main_generate(ac, av);
#elif defined(WITH_TYPED_TEST_RENDER)
	return main_render(ac, av);
#else
#	error "wut"
#endif
}

