#include <learnogl/gl_misc.h>

#include <cxx_prettyprint.hpp>
#include <halpern_pmr/pmr_map.h>

#include <learnogl/app_loop.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/callstack.h>
#include <learnogl/eye.h>
#include <learnogl/gl_timer_query.h>
#include <learnogl/glsl_inspect.h>
#include <learnogl/input_handler.h>
#include <learnogl/renderdoc_app.h>
#include <learnogl/stb_image.h>
#include <learnogl/typed_gl_resources.h>

#include <iostream>

using namespace eng;
using namespace eng::math;

#pragma once

namespace shadow_map
{

    struct ShadowMap {
        eng::FboId recorded_depth_fbo;

        eng::Texture2DHandle recorded_depth_texture;
        eng::SamplerObjectHandle comparing_sampler;
        eng::SamplerObjectHandle visualizing_sampler;

        fo::Vector3 light_position;
        fo::Vector3 light_direction;

        u32 texture_size; // Texture is a square

        // The world, view, projection matrices used during the build-shadow-map pass
        eng::CameraTransformUB eye_block;

        // This transform is used in the lighting pass to get the corresponding texture coordinate of the
        // fragment into the shadow map.
        Mat4 clip_from_world;
    };

    // The info required to set up the shadow map transforms
    struct InitInfo {
        fo::Vector3 light_direction;
        fo::Vector3 light_position;
        u32 texture_size;
        f32 x_extent;
        f32 y_extent;
        f32 neg_z_extent;
    };

    void compute_transforms(ShadowMap &m, const InitInfo &info);

    void init(ShadowMap &m, const InitInfo &init_info)
    {
        auto sampler_desc = eng::default_sampler_desc;
        sampler_desc.set_wrap_mode_all(GL_CLAMP_TO_EDGE);
        m.visualizing_sampler = eng::create_sampler_object(eng::g_rm(), sampler_desc);

        sampler_desc = eng::default_sampler_desc;
        sampler_desc.mag_filter = GL_NEAREST;
        sampler_desc.min_filter = GL_NEAREST;
        sampler_desc.set_wrap_mode_all(GL_CLAMP_TO_BORDER);
        sampler_desc.compare_mode = GL_COMPARE_REF_TO_TEXTURE;

        // Comparison succeeds if argument is less or equal to sampled value.
        sampler_desc.compare_func = GL_LEQUAL;

        m.comparing_sampler = eng::create_sampler_object(eng::g_rm(), sampler_desc);

        eng::TextureCreateInfo texture_ci = {};
        texture_ci.width = init_info.texture_size;
        texture_ci.height = init_info.texture_size;
        texture_ci.texel_info.components = eng::TexelComponents::DEPTH;
        texture_ci.texel_info.internal_type = eng::TexelBaseType::FLOAT;
        texture_ci.texel_info.interpret_type = eng::TexelInterpretType::UNNORMALIZED;

        m.recorded_depth_texture = eng::create_texture_2d(eng::g_rm(), texture_ci);
        m.recorded_depth_fbo = eng::create_fbo(
          eng::g_rm(),
          {},
          m.recorded_depth_texture.rmid(),
          { eng::AttachmentAndClearValue{ eng::NewFBO::CLEAR_DEPTH, Vec4{ 1.0f, 0, 0, 0 } } });

        compute_transforms(m, init_info);
    }

    void compute_transforms(ShadowMap &m, const InitInfo &info)
    {
        // Compute the world to light change of basis

        Vec3 z = negate(info.light_direction);
        Vec3 x, y;
        compute_orthogonal_complements(z, x, y);

#if 1
        // Not really needed, but doing it. Rotate the light axes by 180 deg so that y points "above" the
        // world's z = 0 plane. // <<< Why rotate when you can just negate the z component?
        Plane3 plane(unit_y, 0.0f);
        if (dot(Vec4(plane), y) < 0.0f) {
            auto q = versor_from_axis_angle(z, pi);
            y = apply_versor(q, y);
            x = apply_versor(q, x);
        }
#endif

        Mat4 light_to_world{ Vec4(x, 0), Vec4(y, 0), Vec4(z, 0), Vec4(info.light_position, 1.0f) };

        m.eye_block.view = inverse_rotation_translation(light_to_world);

        // Compute the light to clip space transform. Use an orthographic projection for that purpose.
        m.eye_block.proj.x = Vec4{ 2.0f / info.x_extent, 0.f, 0.f, 0.f };
        m.eye_block.proj.y = Vec4{ 0.f, 2.0f / info.y_extent, 0.f, 0.f };
        m.eye_block.proj.z = Vec4{ 0.f, 0.f, -2.0f / info.neg_z_extent, 0.f };
        m.eye_block.proj.t = Vec4{ 0.f, 0.f, -1.f, 1.f };

        m.light_direction = info.light_direction;
        m.light_position = info.light_position;
    }

    void set_as_draw_fbo(const ShadowMap &m, const eng::FboId &read_fbo)
    {
        eng::bind_destination_fbo(
          eng::g_rm(), m.recorded_depth_fbo, {}); // Should clear depth via this very call.
        eng::bind_source_fbo(eng::g_rm(), read_fbo);
    }

    void set_as_read_fbo(const ShadowMap &m, const eng::FboId &draw_fbo)
    {
        eng::bind_destination_fbo(eng::g_rm(), draw_fbo, {});
        eng::bind_source_fbo(eng::g_rm(), m.recorded_depth_fbo);
    }

    void bind_comparing_sampler(ShadowMap &m, GLuint sampler_bindpoint)
    {
        glBindSampler(sampler_bindpoint, eng::gluint_from_globjecthandle(m.comparing_sampler));
    }

    // Clears the depth texture (fills with 1.0f)
    void clear(ShadowMap &m);

    inline const Mat4 &light_from_world_xform(const ShadowMap &m) { return m.eye_block.view; }
    inline const Mat4 &clip_from_light_xform(const ShadowMap &m) { return m.eye_block.proj; }

    // Returns a transform that takes a point in world space to clip space from light's point of view.
    Mat4 clip_from_world_xform(ShadowMap &m) { return m.clip_from_world; }

} // namespace shadow_map

// Loads a directional light mesh. I just generate the mesh programmatically instead of having it in an
// file. The forward direction points towards negative z. The mesh will drawn with GL_TRIANGLES.
void load_dir_light_mesh(mesh::Model &m)
{
    // eng::load_cube_mesh(m, identity_matrix, true, true);
    // return;

    // Number of faces = 3 * N + 3 * N. No normals and textures.
    push_back(m._mesh_array, {});
    auto &md = back(m._mesh_array);

    const u32 num_divs = 16;
    md.o.num_vertices = 1 + num_divs + 1; // Center, Circumferance points, Tip
    md.o.num_faces = 2 * num_divs;
    md.o.position_offset = 0;
    md.o.normal_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.tex2d_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.packed_attr_size = sizeof(Vec3);
    md.buffer = reinterpret_cast<u8 *>(
      m._buffer_allocator->allocate(sizeof(Vec3) * md.o.num_vertices + sizeof(u16) * md.o.num_faces * 3));

    Vec3 *pos = reinterpret_cast<Vec3 *>(md.buffer);

    f32 current_angle = 0.0f;

    pos[0].x = 0.0f;
    pos[0].y = 0.0f;
    pos[0].z = 0.0f;

    const f32 arc_angle = 2.0f * pi / (f32)num_divs;

    for (u32 i = 1; i <= num_divs; ++i) {
        pos[i].x = std::cos(current_angle);
        pos[i].y = std::sin(current_angle);
        pos[i].z = 1.0f;
        current_angle += arc_angle;
    }

    pos[num_divs + 1] = Vec3{ 0, 0, -1.0f };

    u16 *indices = (u16 *)(md.buffer + md.o.get_indices_byte_offset());

    for (u32 face = 0; face < num_divs; ++face) {
        indices[face * 3] = 0;
        indices[face * 3 + 1] = (face + 1) % num_divs;
        indices[face * 3 + 2] = (face + 2) % num_divs;
    }

    for (u32 face = num_divs, j = 1; face < num_divs * 2; ++face, ++j) {
        indices[face * 3] = num_divs + 1;
        indices[face * 3 + 1] = j;
        indices[face * 3 + 2] = (j + 1) % num_divs;
    }
}

namespace constants
{

    constexpr u32 light_count = 4;
    constexpr i32 window_width = 1280;
    constexpr i32 window_height = 720;

} // namespace constants

struct ShapeSphere {
    Vec3 center;
    f32 radius;
};

struct ShapeCube {
    Vec3 center;
    Vec3 extent;
};

using RenderableShape = ::VariantTable<ShapeSphere, ShapeCube>;

struct Material {
    Vec4 diffuse_albedo;
    Vec3 fresnel_R0;
    f32 shininess;
};

constexpr Vec4 mul_rgb(Vec4 color, float k) { return Vec4(Vec3(color) * k, color.w); }

constexpr Material BALL_MATERIAL = { mul_rgb(colors::Tomato, 5.0f), Vec3{ 0.8f, 0.8f, 0.9f }, 0.7f };
constexpr Material FLOOR_MATERIAL = { mul_rgb(colors::BurlyWood, 3.0f), { 0.1f, 0.1f, 0.4f }, 0.4f };
constexpr Material TROPHY_MATERIAL = { mul_rgb(colors::BurlyWood, 5.0f), Vec3{ 1.0f, 1.0f, 1.0f }, 0.9f };
constexpr Material GIZMO_MATERIAL = { Vec4{ 0.0f, 0.0f, 0.95f, 0.05f }, zero_3, 0.0f };
constexpr Material PINK_MATERIAL = { Vec4{ XYZ(colors::HotPink), 0.5f }, zero_3, 0.0f };
constexpr Material LIGHT_GIZMO_MATERIAL = { colors::White, one_3, 1.0f };

struct PerObjectData {
    Mat4 world_from_local;
    Mat4 world_from_local_inv;
    Material material;
};

struct RenderableData {
    // Everything in this struct remains constant once initialized, except the transforms for moving
    // objects.
    VertexBufferHandle vbo_handle;
    IndexBufferHandle ebo_handle;
    VertexArrayHandle vao_handle;
    Texture2DHandle texture_handle;

    u32 packed_attr_size = 0;
    u32 num_indices = 0;

    // per object uniform data
    PerObjectData uniform_data = { eng::math::identity_matrix, eng::math::identity_matrix, {} };
};

struct DirLightInfo {
    alignas(16) Vec3 position;
    alignas(16) Vec3 direction;
    alignas(16) Vec3 ambient;
    alignas(16) Vec3 diffuse;
    alignas(16) Vec3 specular;

    DirLightInfo() = default;

    DirLightInfo(Vec3 position, Vec3 direction, Vec3 ambient, Vec3 diffuse, Vec3 specular)
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
    Mat4 shadow_xform;
    BoundingSphere scene_bs;
};

// Defining the scene objects using 'shapes', then we make a mesh from the basic shape. This is easier to
// edit in code.
std::vector<RenderableShape> g_scene_objects = {
    {
      ShapeSphere{ Vec3{ 0.0f, 3.2f, -10.0f }, 3.0f },
    },
    {
      ShapeSphere{ Vec3{ 0.0f, 3.2f, 10.0f }, 3.0f },
    },
    {
      ShapeSphere{ Vec3{ 8.0f, 3.2f, 0.0f }, 3.0f },
    },
    {
      ShapeSphere{ Vec3{ -8.0f, 3.2f, 0.0f }, 3.0f },
    },
    {
      ShapeCube{ Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 15.0f, 0.2f, 15.0f } },
    },
    // ^ Floor
};

std::vector<DirLightInfo> g_dir_lights = {};

static float time_in_sec = 0.0f;

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
    RenderableData rd_light_box;
    // Full-screen quad for depth visualization
    RenderableData rd_screen_quad;

    struct {
        UniformBufferHandle camera_ubo;
        UniformBufferHandle dir_lights_list;
        UniformBufferHandle per_object_data;
        UniformBufferHandle shadow_related;
        UniformBufferHandle tonemap_params;
    } uniform_buffers;

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
    } ebos = {};

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
    BindpointsInProgram png_blit_bindpoints;

    shadow_map::ShadowMap shadow_map;

    ShaderDefines shader_defs;

    eng::VertexArrayHandle vao_pos;
    eng::VertexArrayHandle vao_opaque_shapes;

    struct {
        eng::RasterizerStateId without_shadows;
        eng::RasterizerStateId first_pass;
        eng::RasterizerStateId second_pass;
    } rasterizer_states;

    struct {
        eng::DepthStencilStateId without_shadows;
        eng::DepthStencilStateId first_pass;
        eng::DepthStencilStateId second_pass;
    } depth_states;

    struct {
        eng::BlendFunctionDescId no_blend;
    } blend_states;

    bool window_should_close = false;

    eng::gl_timer_query::TimerQueryManager timer_manager;

    fs::path shaders_dir = fs::path(SOURCE_DIR) / "data";

    eng::FboId screen_fbo;

    bool do_blur = false;
    bool do_render_png_blit = false;
};

inline void update_camera_eye_block(App &app, float frame_time_in_sec)
{
    app.eye_block.camera_position = Vec4(app.camera.position(), 1.0f);
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

        app.uniform_buffers.per_object_data = create_uniform_buffer(g_rm(), buffer_ci);
    }

    {
        constexpr size_t shadow_related_buffer_size = sizeof(ShadowRelatedUniforms);

        BufferCreateInfo buffer_ci;
        buffer_ci.bytes = shadow_related_buffer_size;
        buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
        buffer_ci.init_data = nullptr;
        buffer_ci.name = "@shadow_related_ubo";
        app.uniform_buffers.shadow_related = create_uniform_buffer(g_rm(), buffer_ci);
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
    texture_ci.texel_info.internal_type = TexelBaseType::U8;
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
                                 app.uniform_buffers.shadow_related,
                                 SourceToBufferInfo::after_discard(&app.shadow_related, 0));
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
    app.eye_block.camera_position = Vec4(app.camera.position(), 1.0f);

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
    source_to_uniform_buffer(
      g_rm(), app.uniform_buffers.per_object_data, SourceToBufferInfo::after_discard(&rd.uniform_data, 0));
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

void init_shader_defs(App &app)
{
    app.shader_defs.add("NUM_DIR_LIGHTS", (int)constants::light_count);
    app.shader_defs.add("DEPTH_TEXTURE_SIZE", (int)app.shadow_map.texture_size);
}

void read_bindpoints_from_programs(App &app)
{
    // Get the bindpoint of resources from shader.
    LOCAL_FUNC read_bindpoints_for_program = [&](const eng::ShaderProgramHandle &shader_program,
                                                 BindpointsInProgram &bindpoints_out) {
        fo_ss::Buffer shader_sources =
          eng::get_shaders_used_by_program(eng::g_rm(), shader_program).source_paths_as_string(eng::g_rm());

        LOG_F(INFO, "Reading bindpoints for program:\n%s", fo_ss::c_str(shader_sources));

        InspectedGLSL glsl(eng::get_gluint_from_rmid(g_rm(), shader_program.rmid()),
                           eng::pmr_default_resource());

        const auto &uniform_blocks = glsl.GetUniformBlocks();

        for (const auto &block : uniform_blocks) {
            bindpoints_out.uniform_blocks[block.name] = (GLuint)block.bufferBinding;
        }

        for (const auto &uniform : glsl.GetUniforms()) {
            if (uniform.optSamplerInfo) {
                const auto &sampler_info = uniform.optSamplerInfo.value();
                LOG_F(INFO,
                      "Sampler %s bound by default to unit %u",
                      uniform.name.c_str(),
                      sampler_info.textureUnit);
                bindpoints_out.sampled_textures[uniform.name] = sampler_info.textureUnit;
            }
        }

        std::cout << "Uniform blocks: " << bindpoints_out.uniform_blocks << "\n";
        std::cout << "Sampled textures: " << bindpoints_out.sampled_textures << "\n";
        std::cout << "---------------------------------------------------------"
                  << "\n";
    };

    read_bindpoints_for_program(app.shader_programs.with_shadow, app.with_shadow_bindpoints);
    read_bindpoints_for_program(app.shader_programs.build_depth_map, app.build_depth_map_bindpoints);

#if 0
    app.shader_defs.add("PER_OBJECT_UBLOCK_BINDING", (int)app.bound_ubos.per_object.binding);
    app.shader_defs.add("CAMERA_ETC_UBLOCK_BINDING", (int)app.bound_ubos.eye_block.binding);
    app.shader_defs.add("DIR_LIGHTS_LIST_UBLOCK_BINDING", (int)app.bound_ubos.dir_lights_list.binding);
    app.shader_defs.add("FLAT_COLOR", 1);
    app.shader_defs.add("NUM_DIR_LIGHTS", (int)constants::light_count);
    app.shader_defs.add("DEPTH_TEXTURE_UNIT", (int)app.shadow_map.depth_texture_unit);
    app.shader_defs.add("STRUCTURED_TEXTURE_BINDING", (int)app.textures.debug_box_texture.binding);
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
        buffer_info.flags = eng::BufferCreateBitflags::SET_STATIC_STORAGE;
        buffer_info.init_data = (void *)mesh_data.buffer;
        buffer_info.name = vbo_name.c_str();

        VertexBufferHandle vbo_handle = create_vertex_buffer(g_rm(), buffer_info);

        buffer_info.bytes = mesh_data.o.get_indices_size_in_bytes();
        buffer_info.flags = BufferCreateBitflags::SET_STATIC_STORAGE;
        buffer_info.init_data = (void *)(mesh_data.buffer + mesh_data.o.get_indices_byte_offset());
        buffer_info.name = ebo_name.c_str();

        IndexBufferHandle ebo_handle = eng::create_element_array_buffer(g_rm(), buffer_info);

        return std::make_pair(vbo_handle, ebo_handle);
    };

    mesh::Model unit_cube_model;
    eng::load_cube_mesh(unit_cube_model, math::identity_matrix, true, true);
    auto p = build_buffer(unit_cube_model[0], "cube");
    app.vbos.cube = p.first;
    app.ebos.cube = p.second;
    app.stripped_meshes.cube = eng::mesh::StrippedMeshData(unit_cube_model[0].o);

    mesh::Model unit_sphere_model;
    eng::load_sphere_mesh(unit_sphere_model, 30, 30);
    p = build_buffer(unit_sphere_model[0], "sphere");
    app.vbos.sphere = p.first;
    app.ebos.sphere = p.second;
    app.stripped_meshes.sphere = eng::mesh::StrippedMeshData(unit_sphere_model[0].o);

    app.vao_pos = eng::g_rm().pos_vao;
    app.vao_opaque_shapes = eng::g_rm().pnu_vao;

    // Positions of all vertices in the scene
    fo::Array<Vec3> all_positions;
    fo::reserve(all_positions, 512);

    for (const auto &object : g_scene_objects) {
        app.opaque_renderables.push_back(RenderableData{});
        auto &rd = app.opaque_renderables.back();

        VT_SWITCH(object)
        {
            VT_CASE(object, ShapeSphere)
                :
            {
                const auto &sphere = get_value<ShapeSphere>(object);
                auto &world_from_local = rd.uniform_data.world_from_local;
                rd.uniform_data.world_from_local =
                  xyz_scale_matrix(sphere.radius, sphere.radius, sphere.radius);
                translate_update(rd.uniform_data.world_from_local, sphere.center);
                rd.uniform_data.world_from_local_inv = inverse(rd.uniform_data.world_from_local);
                rd.packed_attr_size = app.stripped_meshes.sphere.packed_attr_size;
                rd.num_indices = app.stripped_meshes.sphere.num_faces * 3;
                rd.vbo_handle = app.vbos.sphere;
                rd.ebo_handle = app.ebos.sphere;

                rd.vao_handle = app.vao_opaque_shapes;

                fo::Vector<Vec3> positions;
                fo::reserve(positions, app.stripped_meshes.sphere.num_vertices);

                for (auto itr = mesh::positions_begin(unit_sphere_model[0]),
                          e = mesh::positions_end(unit_sphere_model[0]);
                     itr != e;
                     ++itr) {
                    fo::push_back(positions, transform_point(world_from_local, *itr));
                }

                for (const auto &p : positions) {
                    push_back(all_positions, p);
                }
            }
            break;

            VT_CASE(object, ShapeCube)
                :
            {
                const auto &cube = get_value<ShapeCube>(object);
                auto &world_from_local = rd.uniform_data.world_from_local;

                rd.uniform_data.world_from_local = xyz_scale_matrix(cube.extent);
                translate_update(rd.uniform_data.world_from_local, cube.center);
                rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
                rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
                rd.uniform_data.world_from_local_inv = inverse(rd.uniform_data.world_from_local);
                rd.vbo_handle = app.vbos.cube;
                rd.ebo_handle = app.ebos.cube;
                rd.vao_handle = app.vao_opaque_shapes;

                fo::Vector<Vec3> positions;
                fo::reserve(positions, app.stripped_meshes.cube.num_vertices);

                for (auto itr = mesh::positions_begin(unit_cube_model[0]),
                          e = mesh::positions_end(unit_cube_model[0]);
                     itr != e;
                     ++itr) {
                    fo::push_back(positions, transform_point(world_from_local, *itr));
                }

                for (const auto &p : positions) {
                    push_back(all_positions, p);
                }

                for (const auto &p : positions) {
                    push_back(all_positions, p);
                }
            }
            break;

#if 0

            VT_CASE(object, ShapeModelPath)
                :
            {
                const auto &model_info = get_value<ShapeModelPath>(object);

                rd.uniform_data.world_from_local = xyz_scale_matrix(model_info.scale);

                rd.uniform_data.world_from_local = rd.uniform_data.world_from_local *
                  rotation_matrix(unit_z, model_info.euler_xyz_m.z);
                rd.uniform_data.world_from_local = rd.uniform_data.world_from_local *
                  rotation_matrix(unit_y, model_info.euler_xyz_m.y);
                rd.uniform_data.world_from_local = rd.uniform_data.world_from_local *
                  rotation_matrix(unit_x, model_info.euler_xyz_m.x);

                rd.uniform_data.world_from_local_inv =
                  inverse(rd.uniform_data.world_from_local);

                translate_update(rd.uniform_data.world_from_local, model_info.position);

                print_matrix_classic("Model's matrix", rd.uniform_data.world_from_local);

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
                rd.num_indices = num_indices(mesh_data);
            }
            break;

#endif

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
        rd.uniform_data.world_from_local = translation_matrix(app.scene_bounding_sphere.center) *
          uniform_scale_matrix(app.scene_bounding_sphere.radius);
        rd.uniform_data.world_from_local_inv = inverse(rd.uniform_data.world_from_local);
        rd.uniform_data.material = GIZMO_MATERIAL;
        rd.packed_attr_size = app.stripped_meshes.sphere.packed_attr_size;
        rd.num_indices = app.stripped_meshes.sphere.num_faces * 3;
        rd.vbo_handle = app.vbos.sphere;
        rd.ebo_handle = app.ebos.sphere;
        rd.vao_handle = app.vao_pos;
    }

    // Initialize the screen quad renderable
    {
        const f32 w = (f32)constants::window_width;
        const f32 h = (f32)constants::window_height;
        fo::Array<Vec3> quad_corners({ Vec3{ 0.f, 0.f, 0.f },
                                       Vec3{ w, 0.f, 0.f },
                                       Vec3{ 0.f, h, 0.f },
                                       Vec3{ w, 0.f, 0.f },
                                       Vec3{ w, h, 0.f },
                                       Vec3{ 0.f, h, 0.f } });

        eng::BufferCreateInfo buffer_ci{};
        buffer_ci.bytes = vec_bytes(quad_corners);
        buffer_ci.flags = eng::BufferCreateBitflags::SET_STATIC_STORAGE;
        buffer_ci.name = "vbo@quad_corners";

        app.rd_screen_quad.vbo_handle = eng::create_vertex_buffer(eng::g_rm(), buffer_ci);
        app.rd_screen_quad.packed_attr_size = sizeof(Vec3);

        app.rd_screen_quad.uniform_data.world_from_local =
          orthographic_projection(0.0f, 0.0f, (f32)constants::window_width, (f32)constants::window_height);
    }
}

// ---------
//
// Put some lights into the g_dir_lights list
// ---------
void set_up_lights(App &app)
{
    // Push some lights into the global lights list. The upper 4 quadrants of the bounding sphere each
    // have a light, while the second quadrant contains the light that will cast shadows
    Vec3 center = app.scene_bounding_sphere.center;

    std::vector<Vec3> position_store;
    position_store.reserve(4);

    // First quadrant
    f32 radius = app.scene_bounding_sphere.radius + 20.0f;
    f32 ang_y = 70.0f * one_deg_in_rad;
    f32 ang_zx = 45.0f * one_deg_in_rad;

    Vec3 pos = center +
      Vec3{ radius * std::sin(ang_y) * std::sin(ang_zx),
            radius * std::cos(ang_y),
            radius * std::sin(ang_y) * std::cos(ang_zx) };

    // A convenient light direction for the purpose of this demo is to point at the center of the bounding
    // sphere. Then x_max = r, and x_min = -r. Same for y_max and y_min.

    // Vec3 pos = center + Vec3{radius, radius, radius};
    g_dir_lights.emplace_back(
      pos, center - pos, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.4f, 0.4f, 0.4f), Vec3(0.0f, 0.0f, 0.0f));

    position_store.push_back(pos);

    // Second quadrant
    Mat4 rotation = rotation_about_y(pi / 2.0f);
    pos = Vec3(rotation * Vec4(pos, 1.0f));
    g_dir_lights.emplace_back(
      pos, center - pos, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.9f, 0.9f, 0.9f), Vec3(0.0f, 0.0f, 0.0f));

    position_store.push_back(pos);

    // Third quadrant
    rotation_about_y_update(rotation, 2 * pi / 2.0f);
    pos = Vec3(rotation * Vec4(pos, 1.0f));
    g_dir_lights.emplace_back(
      pos, center - pos, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.2f, 0.2f, 0.2f), Vec3(0.0f, 0.0f, 0.0f));

    position_store.push_back(pos);

    // Fourth quadrant
    rotation_about_y_update(rotation, 3 * pi / 2.0f);
    pos = Vec3(rotation * Vec4(pos, 1.0f));
    g_dir_lights.emplace_back(
      pos, center - pos, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.2f, 0.2f, 0.2f), Vec3(0.0f, 0.0f, 0.0f));

    position_store.push_back(pos);

    CHECK_F(constants::light_count == g_dir_lights.size(),
            "constants::light_count = %u, g_dir_lights.size() = %zu",
            constants::light_count,
            g_dir_lights.size());

    // For each light, insert a cube renderable

    for (size_t i = 0; i < g_dir_lights.size(); ++i) {
        // const auto &l = g_dir_lights[i];

        auto &rd = push_back_get(app.opaque_renderables, RenderableData{});
        rd.vbo_handle = app.vbos.cube;
        rd.ebo_handle = app.ebos.cube;
        rd.uniform_data.world_from_local = translation_matrix(position_store[i]);
        rd.uniform_data.world_from_local_inv = translation_matrix(-position_store[i]);
        rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
        rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
        rd.uniform_data.material = LIGHT_GIZMO_MATERIAL;
    }

    LOG_F(INFO, "Added lights to scene");
}

void set_up_casting_light_gizmo(App &app)
{
    mesh::Model m;
    load_dir_light_mesh(m);

    auto &md = m[0];

    GLuint vbo;
    GLuint ebo;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 md.o.get_vertices_size_in_bytes(),
                 md.buffer + md.o.get_vertices_byte_offset(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 md.o.get_indices_size_in_bytes(),
                 md.buffer + md.o.get_indices_size_in_bytes(),
                 GL_STATIC_DRAW);

    auto &rd = app.rd_casting_light;

    rd.vbo_handle = vbo;
    rd.ebo_handle = ebo;
    rd.packed_attr_size = m[0].o.packed_attr_size;
    rd.num_indices = m[0].o.num_faces * 3;
    rd.uniform_data.material = PINK_MATERIAL;
    rd.uniform_data.world_from_local = inverse_rotation_translation(light_from_world_xform(app.shadow_map));
}

void init_render_states(App &app)
{
    // Rasterizer states (all are same. Might change the depth bias later for second pass)

    auto rs = eng::default_rasterizer_state_desc;
    // rs.slope_scaled_depth_bias = 3.0f;
    // rs.constant_deph_bias = 4;

    app.rasterizer_states.first_pass = eng::create_rs_state(eng::g_rm(), rs);
    app.rasterizer_states.second_pass = app.rasterizer_states.first_pass;
    app.rasterizer_states.without_shadows = app.rasterizer_states.first_pass;

    // Depth stencil states (all are same, really)

    auto ds = eng::default_depth_stencil_desc;
    app.depth_states.without_shadows = eng::create_ds_state(eng::g_rm(), ds);
    app.depth_states.first_pass = app.depth_states.without_shadows;
    app.depth_states.second_pass = app.depth_states.without_shadows;

    // That single blend state, without any blending.
    auto bf = eng::default_blendfunc_state;
    app.blend_states.no_blend = eng::create_blendfunc_state(eng::g_rm(), bf);
}

void init_rasterizer_states(App &app)
{
#if 0
    RasterizerStateDesc rs = default_rasterizer_state_desc;
    app.rasterizer_states.without_shadows = eng::gl().bs.add_rasterizer_state(rs);

    rs = default_rasterizer_state_desc;
    // rs.slope_scaled_depth_bias = 3.0f;
    // rs.constant_depth_bias = 4;
    app.rasterizer_states.first_pass = eng::gl().bs.add_rasterizer_state(rs);

    rs = default_rasterizer_state_desc;
    app.rasterizer_states.second_pass = eng::gl().bs.add_rasterizer_state(rs);
#endif
    eng::RasterizerStateDesc rs = eng::default_rasterizer_state_desc;

    app.rasterizer_states.first_pass = eng::create_rs_state(eng::g_rm(), rs);

    // First pass
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

    Mat4 world_from_light_xform = inverse_rotation_translation(app.shadow_map.eye_block.view);

    // Create a vbo for the lines to denote the light's local axes
    {
        fo::Array<Vec3> line_points;
        resize(line_points, 6);
        line_points[0] = -unit_x;
        line_points[1] = unit_x;
        line_points[2] = -unit_y;
        line_points[3] = unit_y;
        line_points[4] = zero_3;
        line_points[5] = unit_z;
        eng::BufferCreateInfo buffer_ci{};
        buffer_ci.bytes = vec_bytes(line_points);
        buffer_ci.flags = eng::BufferCreateBitflags::SET_STATIC_STORAGE;
        buffer_ci.bytes = vec_bytes(line_points);
        app.rd_light_xyz.vbo_handle = eng::create_vertex_buffer(eng::g_rm(), buffer_ci);
        app.rd_light_xyz.vao_handle = app.vao_pos;

        app.rd_light_xyz.uniform_data.world_from_local = world_from_light_xform *
          xyz_scale_matrix(init_info.x_extent / 2.0f, init_info.y_extent / 2.0f, -init_info.neg_z_extent);

        app.rd_light_xyz.uniform_data.material = BALL_MATERIAL;
        app.rd_light_xyz.packed_attr_size = sizeof(Vec3);
    }

    // Create the renderable for the light bounding box
    {
        auto &rd = app.rd_light_box;
        rd.vbo_handle = app.vbos.cube;
        rd.ebo_handle = app.ebos.cube;

        rd.uniform_data.world_from_local =
          xyz_scale_matrix(Vec3{ init_info.x_extent, init_info.y_extent, init_info.neg_z_extent });
        translate_update(rd.uniform_data.world_from_local, { 0.0f, 0.0f, -init_info.neg_z_extent * 0.5f });
        rd.uniform_data.world_from_local = world_from_light_xform * rd.uniform_data.world_from_local;

        rd.uniform_data.world_from_local_inv = inverse(rd.uniform_data.world_from_local);
        rd.uniform_data.material = PINK_MATERIAL;
        rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
        rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
    }
}

// -----
//
// Renders to the depth map from the point of view of the shadow-casting light
// -----
void render_to_depth_map(App &app)
{
    begin_timer(app.timer_manager, "build_depth_map");
    DEFERSTAT(end_timer(app.timer_manager, "build_depth_map"));

    static bool first_rdoc_captured = false;

    if (!first_rdoc_captured) {
        eng::trigger_renderdoc_frame_capture(1);
        first_rdoc_captured = true;
    }

    glViewport(0, 0, app.shadow_map.texture_size, app.shadow_map.texture_size);

    eng::set_rs_state(eng::g_rm(), app.rasterizer_states.first_pass);
    eng::set_ds_state(eng::g_rm(), app.depth_states.first_pass);
    eng::set_blendfunc_state(eng::g_rm(), 0, app.blend_states.no_blend);

    // Set the depth buffer as current render target, and clear it.
    shadow_map::set_as_draw_fbo(app.shadow_map, eng::FboId::for_default_fbo());

    // Set the eye transforms
    source_eye_block_uniform(app, app.shadow_map.eye_block);

    eng::set_program(eng::g_rm(), app.shader_programs.build_depth_map);

    // Render all opaque objects
    glBindVertexArray(eng::gluint_from_globjecthandle(app.vao_opaque_shapes));

    glBindBuffer(GL_UNIFORM_BUFFER, eng::gluint_from_globjecthandle(app.uniform_buffers.per_object_data));

    glBindBufferRange(GL_UNIFORM_BUFFER,
                      app.with_shadow_bindpoints.uniform_blocks.at("ublock_PerObject"),
                      eng::gluint_from_globjecthandle(app.uniform_buffers.per_object_data),
                      0,
                      sizeof(PerObjectData));

    for (const auto range : app.shape_ranges) {
        auto &rd0 = app.opaque_renderables[range.first];

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eng::gluint_from_globjecthandle(rd0.ebo_handle));
        glBindVertexBuffer(0, eng::gluint_from_globjecthandle(rd0.vbo_handle), 0, rd0.packed_attr_size);

        for (size_t i = range.first; i < range.second; ++i) {
            auto &rd = app.opaque_renderables[i];
            source_per_object_uniforms(app, rd);

            glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
        }
    }

    // Restore screen fbo
    shadow_map::set_as_read_fbo(app.shadow_map, app.screen_fbo);
}

// -----------
//
// Visualize the depth map by blitting it to the screen
// -----------
#if 0
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
    glBindVertexArray(eng::gluint_from_globjecthandle(app.pos_vao));
    glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

#endif

// Renders the objects along with shadows.
void render_second_pass(App &app)
{
    glUseProgram(eng::gluint_from_globjecthandle(app.shader_programs.with_shadow));

    begin_timer(app.timer_manager, "second_pass");
    DEFERSTAT(end_timer(app.timer_manager, "second_pass"));

    eng::bind_destination_fbo(eng::g_rm(), app.screen_fbo, { -1 });
    eng::set_rs_state(eng::g_rm(), app.rasterizer_states.first_pass);
    eng::set_ds_state(eng::g_rm(), app.depth_states.second_pass);

    glDisable(GL_BLEND);

    glViewport(0, 0, constants::window_width, constants::window_height);

    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    app.shadow_related.shadow_xform = app.shadow_map.clip_from_world;
    app.shadow_related.scene_bs = app.scene_bounding_sphere;

    const auto shadow_related_gluint = eng::gluint_from_globjecthandle(app.uniform_buffers.shadow_related);

    source_to_uniform_buffer(
      eng::g_rm(),
      app.uniform_buffers.shadow_related,
      eng::SourceToBufferInfo::after_discard(reinterpret_cast<void *>(&app.shadow_related), 0));

    glBindBuffer(GL_UNIFORM_BUFFER, shadow_related_gluint);

    // Source the main camera's eye block.
    source_eye_block_uniform(app, app.eye_block);

    {
        // Only difference with render_without_shadow is that we use the other shader program. Factor this
        // out?
        eng::set_program(eng::g_rm(), app.shader_programs.with_shadow);

        glBindBuffer(GL_UNIFORM_BUFFER, shadow_related_gluint);

        glBindBufferRange(GL_UNIFORM_BUFFER,
                          app.with_shadow_bindpoints.uniform_blocks.at("ShadowRelated"),
                          shadow_related_gluint,
                          0,
                          sizeof(ShadowRelatedUniforms));

        shadow_map::bind_comparing_sampler(app.shadow_map,
                                           app.with_shadow_bindpoints.sampled_textures["comparing_sampler"]);

        DCHECK_NE_F(app.opaque_renderables[0].vao_handle.rmid(), 0);

        glBindVertexArray(eng::gluint_from_globjecthandle(app.opaque_renderables[0].vao_handle));

        // Lights

        const auto dir_lights_gluint = eng::gluint_from_globjecthandle(app.uniform_buffers.dir_lights_list);

        glBindBuffer(GL_UNIFORM_BUFFER, dir_lights_gluint);
        glBindBufferRange(GL_UNIFORM_BUFFER,
                          app.with_shadow_bindpoints.uniform_blocks.at("DirLightsList"),
                          dir_lights_gluint,
                          0,
                          vec_bytes(g_dir_lights));

        // Per object data
        const auto per_object_data_gluint =
          eng::gluint_from_globjecthandle(app.uniform_buffers.per_object_data);

        glBindBuffer(GL_UNIFORM_BUFFER, per_object_data_gluint);

        glBindBufferRange(GL_UNIFORM_BUFFER,
                          app.with_shadow_bindpoints.uniform_blocks.at("ublock_PerObject"),
                          per_object_data_gluint,
                          0,
                          sizeof(PerObjectData));

        for (const auto range : app.shape_ranges) {
            auto &rd0 = app.opaque_renderables[range.first];
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eng::gluint_from_globjecthandle(rd0.ebo_handle));
            glBindVertexBuffer(0, eng::gluint_from_globjecthandle(rd0.vbo_handle), 0, rd0.packed_attr_size);

            for (size_t i = range.first; i < range.second; ++i) {
                auto &rd = app.opaque_renderables[i];
                source_per_object_uniforms(app, rd);

                // Bind the per-object uniform

                glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
            }
        }
    }
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

#if 0
    if (app.do_blur) {
        render_post_blur(app);
        if (!captured_once) {
            eng::trigger_renderdoc_frame_capture(1);
            captured_once = true;
        }
    }

#endif

    // tonemap_to_backbuffer(app);
}

#if 0
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

#endif

void sort_shape_vbos(App &app)
{
    std::sort(app.opaque_renderables.begin(),
              app.opaque_renderables.end(),
              [](const RenderableData &rd1, const RenderableData &rd2) {
                  return rd1.vbo_handle.rmid() < rd2.vbo_handle.rmid();
              });

    size_t start = 0;
    size_t i = start + 1;

    do {
        while (i < app.opaque_renderables.size() &&
               app.opaque_renderables[i - 1].vbo_handle.rmid() ==
                 app.opaque_renderables[i].vbo_handle.rmid()) {
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

    template <> void init<App>(App &app)
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

        app.screen_fbo =
          eng::create_default_fbo(eng::g_rm(), { eng::AttachmentAndClearValue{ 0, ::colors::AliceBlue } });

        REGISTER_GLFW_CALLBACKS(app, eng::gl().window);

        build_geometry_buffers_and_bounding_sphere(app);
        load_debug_box_texture(app);
        set_up_lights(app);
        create_uniform_buffers(app);
        init_shadow_map(app);
        init_uniform_data(app);
        set_up_camera(app);
        set_up_casting_light_gizmo(app);

        sort_shape_vbos(app);

        init_render_states(app);

        init_shader_defs(app);

        LOG_F(INFO, "Shader defines =\n%s", app.shader_defs.get_string().c_str());

        load_shadow_map_programs(app);
        // load_tonemap_program(app);
        // load_png_blit_program(app);

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

    template <> void update<App>(App &app, State &state)
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

    template <> void render<App>(App &app)
    {
        eng::gl_timer_query::new_frame(app.timer_manager);

        render_with_shadow(app);

        {
            eng::gl_timer_query::begin_timer(app.timer_manager, "swap_buffer");
            glfwSwapBuffers(eng::gl().window);
            eng::gl_timer_query::end_timer(app.timer_manager, "swap_buffer");
        }

        eng::gl_timer_query::end_frame(app.timer_manager);

    } // namespace app_loop

    template <> void close<App>(App &app)
    {
        eng::gl_timer_query::wait_for_last_frame(app.timer_manager);

        fo::string_stream::Buffer ss(fo::memory_globals::default_allocator());
        eng::gl_timer_query::print_times(app.timer_manager, ss);
        LOG_F(INFO, "\n%s", fo::string_stream::c_str(ss));

        // imgl3_shutdown();
        // imglfw_shutdown();
    }

    template <> bool should_close<App>(App &app)
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
    glparams.load_renderdoc = true;
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

