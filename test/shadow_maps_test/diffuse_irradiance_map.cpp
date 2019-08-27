// Goals  - 'Small architecture'. PBR intro.

#include "diffuse_irradiance_map.h"
#include "diffuse_irradiance_utils.inc.h"

#include <learnogl/app_loop.h>
#include <learnogl/eng>
#include <learnogl/mesh.h>

using namespace eng::math;

const int window_width = 1280;
const int window_height = 720;

struct App {
    eng::input::BaseHandlerPtr<App> input_handler = eng::input::make_default_handler<App>();
    var_ &current_input_handler() { return self_.input_handler; }

    eng::FboId default_fbo;

    Geometry torus_geometry;

    BasicPipeline first_try_pip;

    struct UniformBuffers {
        eng::UniformBufferHandle camera_transforms;
    };

    UniformBuffers uniform_buffers;

    eng::Camera main_camera;

    void create_meshes()
    {
        {
            var_ pmesh = par_shapes_create_torus(20, 20, 1.0);
            DEFERSTAT(par_shapes_free_mesh(pmesh));
            par_shapes_compute_normals(pmesh);
            torus_geometry.init_from_par_mesh(pmesh, "torus");
        }
    }

    void create_camera_uniform_buffer()
    {
        eng::BufferCreateInfo ci;
        ci.bytes = sizeof(eng::CameraTransformUB);
        ci.flags = eng::BufferCreateBitflags::SET_DYNAMIC_STORAGE;
        ci.init_data = nullptr;
        ci.name = "ubo@camera_transform";

        self_.uniform_buffers.camera_transforms = eng::create_uniform_buffer(eng::g_rm(), ci);
    }

    void create_pipelines()
    {
        self_.first_try_pip.init(source_dir / "data/pos_only.vert",
                                 source_dir / "data/usual_fs.frag",
                                 eng::g_rm().pos_vao,
                                 { ResourceSlotInfo::denoting_buffer(0, 0, "CameraTransforms") },
                                 {});

        eng::RasterizerStateDesc rs_desc = eng::default_rasterizer_state_desc;
        rs_desc.cull_side = GL_NONE;
        self_.first_try_pip.rs_state_id = eng::create_rs_state(eng::g_rm(), rs_desc);

        eng::DepthStencilStateDesc ds_desc = eng::default_depth_stencil_desc;
        self_.first_try_pip.ds_state_id = eng::create_ds_state(eng::g_rm(), ds_desc);

        eng::BlendFunctionDesc blend_desc = eng::default_blendfunc_state;
        self_.first_try_pip.blend_state_id = eng::create_blendfunc_state(eng::g_rm(), blend_desc);
    }

    void render()
    {
        self_.first_try_pip.set_shader_and_render_state();
        self_.torus_geometry.bind_geometry_buffers(true);
    }
};

DEFINE_GLFW_CALLBACKS(App);

namespace app_loop
{
    template <> void init<App>(App &a)
    {
        REGISTER_GLFW_CALLBACKS(a, eng::gl().window);

        eng::create_default_fbo(eng::g_rm(),
                                { eng::AttachmentAndClearValue(eng::depth_attachment(), Vec4(1, 0, 0, 0)),
                                  eng::AttachmentAndClearValue(0, colors::AliceBlue) });

        a.create_meshes();
        a.create_pipelines();

        a.main_camera.set_proj(
          0.2f, 1000.0f, 70.0f * eng::math::one_deg_in_rad, f32(window_width) / window_height);

        a.main_camera.look_at(eng::math::zero_3, Vec3(0, 0, 1.0), eng::math::unit_y);
    }

    template <> void update<App>(App &a, ::app_loop::State &s) {
        glfwPollEvents();
    }

    template <> void render<App>(App &a) {


        glfwSwapBuffers(eng::gl().window);
}

    template <> void close<App>(App &a) {}

    template <> bool should_close<App>(App &a) { return false; }

} // namespace app_loop

int main() {

}
