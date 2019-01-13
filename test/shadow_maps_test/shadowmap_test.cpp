// Shadow-mapping algorithm demo.

#include "demo_types.h"
#include "shadow_map.h"

#include <learnogl/renderdoc_app.h>

// Defining the scene objects using 'shapes', then we make a mesh from the basic shape. This is easier to edit
// in code.
std::vector<RenderableShape> g_scene_objects = {
    // renderable_shapes::Sphere{Vector3{-0.0f, 3.2f, 0.0f}, 3.0f},
    renderable_shapes::Sphere{Vector3{0.0f, 5.0f, -10.0f}, 3.0f},
    renderable_shapes::Sphere{Vector3{0.0f, 3.2f, 10.0f}, 3.0f},
    renderable_shapes::Sphere{Vector3{8.0f, 3.2f, 0.0f}, 3.0f},
    renderable_shapes::Sphere{Vector3{-8.0f, 3.2f, 0.0f}, 3.0f},
    // renderable_shapes::Cube{Vector3{-8.0f, 3.2f, 0.0f}, Vector3{3.0f, 2.0f, 3.0f}},
    renderable_shapes::Cube{Vector3{0.0f, 0.0f, 0.0f}, Vector3{15.0f, 0.2f, 15.0f}},
    // ^ Floor
    renderable_shapes::ModelPath((fs::path(SOURCE_DIR) / "data") / "trophydeco.obj",
                                 one_3 * 0.2f,
                                 Vector3{-(f32)pi / 2, 0.0f, 0.0f},
                                 zero_3)};

std::vector<DirLightInfo> g_dir_lights = {};

enum Mode {
    NO_SHADOWS = 0,
    LIGHT_VIEW,
    DEPTH_VIEW,
    WITH_SHADOWS,

    NUM_MODES
};

struct App {
    input::BaseHandlerPtr<App> input_handler = input::make_handler<input::InputHandlerBase<App>>();

    auto &current_input_handler() { return input_handler; }
    void set_input_handler(input::BaseHandlerPtr<App> ptr) { input_handler = std::move(ptr); }

    Camera camera;
    uniform_formats::EyeBlock eye_block;
    uniform_formats::EyeBlock eye_block_for_blitting;

    std::vector<RenderableData> opaque_renderables;
    BoundingSphere scene_bounding_sphere;

    RenderableData rd_scene_bs;
    RenderableData rd_casting_light;
    RenderableData rd_light_xyz;
    RenderableData rd_sphere_axes;
    RenderableData rd_light_box;
    // Full-screen quad for depth visualization
    RenderableData rd_screen_quad;

    // The binding points of each uniform buffer range
    struct {
        BoundUBO eye_block;
        BoundUBO dir_lights_list;
        BoundUBO per_object;
    } ublock_bindings;

    struct {
        BoundTexture structured_texture;
    } textures;

    bool light_attribs_changed = true;

    struct {
        GLuint cube;
        GLuint sphere;
    } vbos = {};

    struct {
        GLuint cube;
        GLuint sphere;
    } ebos = {};

    struct {
        mesh::StrippedMeshData cube;
        mesh::StrippedMeshData sphere;
    } stripped_meshes = {};

    GLuint vao_pos_normal_st;
    GLuint vao_pos;

    struct {
        GLuint no_shadow;
        GLuint no_lights;
        GLuint build_depth_map;
        GLuint blit_depth_map;
        GLuint structured_textured_use;
        GLuint with_shadow;
    } shader_programs = {};

    struct {
        GLint shadow_xform;
    } uniform_locs;

    // Programs
    GLuint basic_shapes_shadow_pass;
    GLuint basic_shapes_draw_pass;

    shadow_map::ShadowMap shadow_map;

    ShaderDefines shader_defs;

    struct {
        u32 no_shadows;
        u32 first_pass;
        u32 second_pass;
    } rasterizer_states;

    GLFWwindow *window;
    bool window_should_close;

    Mode mode = Mode::NO_SHADOWS;

    fs::path shaders_dir = fs::path(SOURCE_DIR) / "data";
};

inline void update_camera_eye_block(App &app, float frame_time_in_sec) {
    app.eye_block.frame_time_in_sec = frame_time_in_sec;
    app.eye_block.eye_pos = app.camera.position();
    app.eye_block.view_from_world_xform = app.camera.view_xform();
}

// Creates all the uniform buffers and binds them to separate binding points.
void create_uniform_buffers(App &app) {
    // Could create a single buffer and allocate blocks from there. But not bothering.
    {
        GLuint eye_block_buffer;

        glGenBuffers(1, &eye_block_buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, eye_block_buffer);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(uniform_formats::EyeBlock), nullptr, GL_DYNAMIC_DRAW);

        app.ublock_bindings.eye_block.desc =
            gl_desc::UniformBuffer(eye_block_buffer, 0, sizeof(uniform_formats::EyeBlock));

        app.ublock_bindings.eye_block.binding =
            default_binding_state().bind_unique(app.ublock_bindings.eye_block.desc);

        LOG_F(INFO, "CAMERA_ETC_UBLOCK_BINDING = %u", app.ublock_bindings.eye_block.binding);
    }

    {
        constexpr size_t dir_lights_buffer_size =
            sizeof(uniform_formats::DirLightsList<demo_constants::light_count>);

        GLuint dir_lights_list_buffer;
        glGenBuffers(1, &dir_lights_list_buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, dir_lights_list_buffer);
        glBufferData(GL_UNIFORM_BUFFER, dir_lights_buffer_size, nullptr, GL_STATIC_DRAW);

        app.ublock_bindings.dir_lights_list.desc =
            gl_desc::UniformBuffer(dir_lights_list_buffer, 0, dir_lights_buffer_size);

        app.ublock_bindings.dir_lights_list.binding =
            default_binding_state().bind_unique(app.ublock_bindings.dir_lights_list.desc);

        LOG_F(INFO, "DIR_LIGHTS_LIST_UBLOCK_BINDING = %u", app.ublock_bindings.dir_lights_list.binding);
    }

    {
        constexpr size_t per_object_buffer_size = sizeof(uniform_formats::PerObject);
        GLuint per_object_uniforms_buffer;

        glGenBuffers(1, &per_object_uniforms_buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, per_object_uniforms_buffer);
        glBufferData(GL_UNIFORM_BUFFER, per_object_buffer_size, nullptr, GL_DYNAMIC_DRAW);

        app.ublock_bindings.per_object.desc =
            gl_desc::UniformBuffer(per_object_uniforms_buffer, 0, per_object_buffer_size);

        app.ublock_bindings.per_object.binding =
            default_binding_state().bind_unique(app.ublock_bindings.per_object.desc);

        LOG_F(INFO, "PER_OBJECT_UBLOCK_BINDING = %u", app.ublock_bindings.per_object.binding);
    }
}

void load_structured_texture(App &app) {
    GLuint tex;
    glGenTextures(1, &tex);
    GLuint binding = default_binding_state().bind_unique(gl_desc::SampledTexture(tex));
    // ^ Binds the texture unit too
    glBindTexture(GL_TEXTURE_2D, tex);

    int x, y, channels;
    auto texture_path = fs::path(SOURCE_DIR) / "data" / "structured_texture.png";
    stbi_set_flip_vertically_on_load(1);
    u8 *pixels = stbi_load(texture_path.u8string().c_str(), &x, &y, &channels, 4);
    CHECK_EQ_F(channels, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    app.textures.structured_texture.desc = gl_desc::SampledTexture(tex);
    app.textures.structured_texture.binding = binding;
}

void load_no_shadow_program(App &app) {
    const auto def_str = app.shader_defs.get_string();
    GLuint vertex_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                             "#version 430 core\n",
                                             GL_VERTEX_SHADER,
                                             def_str.c_str(),
                                             app.shaders_dir / "usual_vs.vert");

    GLuint fragment_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                               "#version 430 core\n",
                                               GL_FRAGMENT_SHADER,
                                               def_str.c_str(),
                                               app.shaders_dir / "no_shadow.frag");

    app.shader_programs.no_shadow = eng::create_program(vertex_shader_obj, fragment_shader_obj);
}

void load_no_lights_program(App &app) {
    const auto defs_str = app.shader_defs.get_string();

    LOG_F(INFO, "no_lights defs =\n%s", defs_str.c_str());

    {
        GLuint vertex_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                 "#version 430 core\n",
                                                 GL_VERTEX_SHADER,
                                                 defs_str.c_str(),
                                                 app.shaders_dir / "pos_only.vert");

        GLuint fragment_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                   "#version 430 core\n",
                                                   GL_FRAGMENT_SHADER,
                                                   defs_str.c_str(),
                                                   app.shaders_dir / "no_lights.frag");

        app.shader_programs.no_lights = eng::create_program(vertex_shader_obj, fragment_shader_obj);
    }
    {
        GLuint vertex_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                 "#version 430 core\n",
                                                 GL_VERTEX_SHADER,
                                                 defs_str.c_str(),
                                                 app.shaders_dir / "usual_vs.vert");

        GLuint fragment_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                   "#version 430 core\n",
                                                   GL_FRAGMENT_SHADER,
                                                   defs_str.c_str(),
                                                   app.shaders_dir / "structured_texture.frag");

        app.shader_programs.structured_textured_use =
            eng::create_program(vertex_shader_obj, fragment_shader_obj);
    }
}

void load_shadow_map_programs(App &app) {
    const auto defs_str = app.shader_defs.get_string();

    LOG_F(INFO, "shadow_map defs =\n%s", defs_str.c_str());

    // Program that builds depth map
    {
        GLuint vertex_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                 "#version 430 core\n",
                                                 GL_VERTEX_SHADER,
                                                 defs_str.c_str(),
                                                 app.shaders_dir / "pos_only.vert");

        GLuint fragment_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                   "#version 430 core\n",
                                                   GL_FRAGMENT_SHADER,
                                                   defs_str.c_str(),
                                                   app.shaders_dir / "build_depth_map.frag");

        app.shader_programs.build_depth_map = eng::create_program(vertex_shader_obj, fragment_shader_obj);
    }

    // Program that simply blits the depth map to the screen
    {
        GLuint vertex_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                 "#version 430 core\n",
                                                 GL_VERTEX_SHADER,
                                                 defs_str.c_str(),
                                                 app.shaders_dir / "pos_only.vert");

        GLuint fragment_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                   "#version 430 core\n",
                                                   GL_FRAGMENT_SHADER,
                                                   defs_str.c_str(),
                                                   app.shaders_dir / "blit_depth_map.frag");

        app.shader_programs.blit_depth_map = eng::create_program(vertex_shader_obj, fragment_shader_obj);
    }

    // Program that renders the scene along with lights and shadows
    {
        GLuint vertex_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                 "#version 430 core\n",
                                                 GL_VERTEX_SHADER,
                                                 defs_str.c_str(),
                                                 app.shaders_dir / "with_shadow.vert");

        GLuint fragment_shader_obj = create_shader(CreateShaderOptionBits::k_prepend_version,
                                                   "#version 430 core\n",
                                                   GL_FRAGMENT_SHADER,
                                                   defs_str.c_str(),
                                                   app.shaders_dir / "with_shadow.frag");

        app.shader_programs.with_shadow = eng::create_program(vertex_shader_obj, fragment_shader_obj);
        app.uniform_locs.shadow_xform = glGetUniformLocation(app.shader_programs.with_shadow, "shadow_xform");

        // Sourcing the shadow transform
        Matrix4x4 shadow_xform = shadow_map::clip_from_world_xform(app.shadow_map);
        glUseProgram(app.shader_programs.with_shadow);
        glUniformMatrix4fv(
            app.uniform_locs.shadow_xform, 1, GL_FALSE, reinterpret_cast<float *>(&shadow_xform));
    }
}

void set_up_camera(App &app) {
    const float distance = 20.0f;
    const float y_extent = 15.0f;

    app.camera.set_proj(0.1f,
                        1000.0f,
                        2.0f * std::atan(0.5f * y_extent / distance),
                        // 70.0f * one_deg_in_rad,
                        demo_constants::window_height / float(demo_constants::window_width));

    app.camera._eye = eye::toward_negz(10.0f);
    app.camera.update_view_transform();

    // Only need to store the camera's projection once
    app.eye_block.clip_from_view_xform = app.camera.proj_xform();

    // The quad blitting eye block also needs to be initialized. Using identity matrices for these two. The
    // ortho projection will be done via the world transform.
    app.eye_block_for_blitting.view_from_world_xform = identity_matrix;
    app.eye_block_for_blitting.clip_from_view_xform = identity_matrix;
}

// ------------
//
// Uniform data initialization
// ------------
Fn init_uniform_data(App &app) {
    // EyeBlock
    app.eye_block.frame_time_in_sec = 0.0f;

    app.eye_block.view_from_world_xform = app.camera.view_xform();
    app.eye_block.clip_from_view_xform = app.camera.proj_xform();
    app.eye_block.eye_pos = app.camera.position();

    glBindBuffer(GL_UNIFORM_BUFFER, app.ublock_bindings.eye_block.handle());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(uniform_formats::EyeBlock), &app.eye_block);
    glBindBuffer(GL_UNIFORM_BUFFER, app.ublock_bindings.dir_lights_list.handle());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, vec_bytes(g_dir_lights), g_dir_lights.data());

    CHECK_NE_F(vec_bytes(g_dir_lights), 0);
}

// ---------------
//
// Send the uniform data for the object to the uniform buffer
// ---------------
Fn source_per_object_uniforms(App &app, const RenderableData &rd) {
    glBindBuffer(GL_UNIFORM_BUFFER, app.ublock_bindings.per_object.handle());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(uniform_formats::PerObject), &rd.uniforms);
}

// ---------------
//
// Source the given eye block uniform to the uniform buffer
// ---------------
Fn source_eye_block_uniform(App &app, const uniform_formats::EyeBlock &eye_block) {
    glBindBuffer(GL_UNIFORM_BUFFER, app.ublock_bindings.eye_block.handle());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(uniform_formats::EyeBlock), &eye_block);
}

// ---- Input handlers

class BasicInputHandler : public input::InputHandlerBase<App> {
  private:
    struct {
        double x, y;
    } _prev_mouse_pos;

  public:
    BasicInputHandler(App &app) { glfwGetCursorPos(app.window, &_prev_mouse_pos.x, &_prev_mouse_pos.y); }

    void handle_on_key(App &app, input::OnKeyArgs args) override;

    void handle_on_mouse_move(App &app, input::OnMouseMoveArgs args) override;

    void handle_on_mouse_button(App &, input::OnMouseButtonArgs args) override;
};

void BasicInputHandler::handle_on_key(App &app, input::OnKeyArgs args) {
    if (args.key == GLFW_KEY_ESCAPE && args.action == GLFW_PRESS) {
        app.window_should_close = true;
    }

    if (args.key == GLFW_KEY_T && args.action == GLFW_RELEASE) {
        app.mode = Mode((app.mode + 1) % Mode::NUM_MODES);
    }
}

void BasicInputHandler::handle_on_mouse_move(App &app, input::OnMouseMoveArgs args) {
    int state = glfwGetMouseButton(app.window, GLFW_MOUSE_BUTTON_RIGHT);
    if (state == GLFW_PRESS) {
        float diff = (float)(args.ypos - _prev_mouse_pos.y);
        float angle = -0.25f * one_deg_in_rad * diff;
        // app.camera.pitch(angle);

        diff = (float)(args.xpos - _prev_mouse_pos.x);
        angle = -0.25f * one_deg_in_rad * diff;
        app.camera.yaw(angle);
    }
    _prev_mouse_pos = {args.xpos, args.ypos};
}

void BasicInputHandler::handle_on_mouse_button(App &app, input::OnMouseButtonArgs args) {
    if (args.button == GLFW_MOUSE_BUTTON_RIGHT && args.action == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(app.window, &xpos, &ypos);
        _prev_mouse_pos = {xpos, ypos};
    }
}

DEFINE_GLFW_CALLBACKS(App);

Fn init_shader_defines(App &app) {
    app.shader_defs.add("PER_OBJECT_UBLOCK_BINDING", (int)app.ublock_bindings.per_object.binding);
    app.shader_defs.add("CAMERA_ETC_UBLOCK_BINDING", (int)app.ublock_bindings.eye_block.binding);
    app.shader_defs.add("DIR_LIGHTS_LIST_UBLOCK_BINDING", (int)app.ublock_bindings.dir_lights_list.binding);
    app.shader_defs.add("FLAT_COLOR", 1);
    app.shader_defs.add("NUM_DIR_LIGHTS", (int)demo_constants::light_count);
    app.shader_defs.add("DEPTH_TEXTURE_UNIT", (int)app.shadow_map.depth_texture_unit);
    app.shader_defs.add("STRUCTURED_TEXTURE_BINDING", (int)app.textures.structured_texture.binding);
    app.shader_defs.add("DEPTH_TEXTURE_SIZE", (int)app.shadow_map.texture_size);
}

void build_geometry_buffers_and_bounding_sphere(App &app) {
    const auto build_buffer = [&](const mesh::MeshData &mesh_data) {
        GLuint vbo_ebo[2];

        glGenBuffers(2, vbo_ebo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_ebo[0]);

        const size_t vbo_size = mesh::vertex_buffer_size(mesh_data);
        glBufferData(GL_ARRAY_BUFFER, vbo_size, mesh_data.buffer, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo_ebo[1]);

        const size_t ebo_size = mesh::index_buffer_size(mesh_data);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo_size, mesh::indices(mesh_data), GL_STATIC_DRAW);

        return std::make_pair(vbo_ebo[0], vbo_ebo[1]);
    };

    mesh::Model unit_cube_model;
    eng::load_cube_mesh(unit_cube_model, math::identity_matrix, true, true);
    auto p = build_buffer(unit_cube_model[0]);
    app.vbos.cube = p.first;
    app.ebos.cube = p.second;
    app.stripped_meshes.cube = unit_cube_model[0];

    mesh::Model unit_sphere_model;
    eng::load_sphere_mesh(unit_sphere_model);
    p = build_buffer(unit_sphere_model[0]);
    app.vbos.sphere = p.first;
    app.ebos.sphere = p.second;
    app.stripped_meshes.sphere = unit_sphere_model[0];

    // Generate the vaos
    app.vao_pos_normal_st =
        eng::gen_vao({eng::VaoFloatFormat(0, 3, GL_FLOAT, GL_FALSE, 0),
                          eng::VaoFloatFormat(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3)),
                          eng::VaoFloatFormat(3, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(Vector3))});

    app.vao_pos = eng::gen_vao({eng::VaoFloatFormat(0, 3, GL_FLOAT, GL_FALSE, 0)});

    // Positions of all vertices in the scene
    Array<Vector3> all_positions(memory_globals::default_allocator());
    reserve(all_positions, 512);

    for (const auto &shape : g_scene_objects) {
        app.opaque_renderables.push_back(RenderableData{});
        auto &rd = app.opaque_renderables.back();

        switch (type_index(shape)) {
        case 0: {
            const auto &sphere = get_value<renderable_shapes::Sphere>(shape);
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
        } break;

        case 1: {
            const auto &cube = get_value<renderable_shapes::Cube>(shape);
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
        } break;

        case 2: {
            const auto &model_info = get_value<renderable_shapes::ModelPath>(shape);
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
                               Vector2{0.0f, 0.0f},
                               mesh::CALC_TANGENTS | mesh::TRIANGULATE | mesh::CALC_NORMALS |
                                   mesh::FILL_CONST_UV));
            // CHECK_EQ_F(size(model._mesh_array), 1);
            auto &mesh_data = model[0];

            glGenBuffers(1, &rd.vbo);
            glGenBuffers(1, &rd.ebo);
            glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
            glBufferData(GL_ARRAY_BUFFER, vertex_buffer_size(mesh_data), mesh_data.buffer, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER, index_buffer_size(mesh_data), indices(mesh_data), GL_STATIC_DRAW);

            CHECK_NE_F(mesh_data.normal_offset, mesh::ATTRIBUTE_NOT_PRESENT);
            CHECK_NE_F(mesh_data.tex2d_offset, mesh::ATTRIBUTE_NOT_PRESENT);

            rd.packed_attr_size = mesh_data.packed_attr_size;
            rd.vao = app.vao_pos_normal_st;
            rd.num_indices = num_indices(mesh_data);
        } break;

        default:
            assert(false && "Not implemented for this shape");
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

        Array<Vector3> line_points(memory_globals::default_allocator(),
                                   {-unit_x, unit_x, -unit_y, unit_y, -unit_z, unit_z});

        glBufferData(GL_ARRAY_BUFFER, vec_bytes(line_points), data(line_points), GL_STATIC_DRAW);

        rd.uniforms.world_from_local_xform = translation_matrix(app.scene_bounding_sphere.center) *
                                             uniform_scale_matrix(app.scene_bounding_sphere.radius);

        rd.packed_attr_size = sizeof(Vector3);
    }

    // Initialize the screen quad renderable
    {
        const f32 w = (f32)demo_constants::window_width;
        const f32 h = (f32)demo_constants::window_height;
        Array<Vector3> quad_corners(memory_globals::default_allocator(),
                                    {Vector3{0.f, 0.f, 0.f},
                                     Vector3{w, 0.f, 0.f},
                                     Vector3{0.f, h, 0.f},
                                     Vector3{w, 0.f, 0.f},
                                     Vector3{w, h, 0.f},
                                     Vector3{0.f, h, 0.f}});

        glGenBuffers(1, &app.rd_screen_quad.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, app.rd_screen_quad.vbo);
        glBufferData(GL_ARRAY_BUFFER, vec_bytes(quad_corners), data(quad_corners), GL_STATIC_DRAW);
        app.rd_screen_quad.vao = app.vao_pos;
        app.rd_screen_quad.packed_attr_size = sizeof(Vector3);
        app.rd_screen_quad.uniforms.world_from_local_xform = orthographic_projection(
            0.0f, 0.0f, (f32)demo_constants::window_width, (f32)demo_constants::window_height);
    }
}

Fn set_up_materials(App &app) {
    CHECK_EQ_F(app.opaque_renderables.size(), g_scene_objects.size());
    for (u32 i = 0; i < g_scene_objects.size(); ++i) {
        switch (type_index(g_scene_objects[i])) {
        case 0: {
            app.opaque_renderables[i].uniforms.material = BALL_MATERIAL;
        } break;
        case 1: {
            app.opaque_renderables[i].uniforms.material = FLOOR_MATERIAL;
        } break;
        case 2: {
            app.opaque_renderables[i].uniforms.material = FLOOR_MATERIAL;
        } break;
        default:
            CHECK_F(false);
        }
    }
}

// ---------
//
// Put some lights into the g_dir_lights list
// ---------
Fn set_up_lights(App &app) {
    // Push some lights into the global lights list. The upper 4 quadrants of the bounding sphere each
    // have a light, while the second quadrant contains the light that will cast shadows
    Vector3 center = app.scene_bounding_sphere.center;

    // First quadrant
    f32 radius = app.scene_bounding_sphere.radius + 5.0f;
    f32 ang_y = 70.0f * one_deg_in_rad;
    f32 ang_zx = 45.0f * one_deg_in_rad;

    Vector3 pos = center + Vector3{radius * std::sin(ang_y) * std::sin(ang_zx),
                                   radius * std::cos(ang_y),
                                   radius * std::sin(ang_y) * std::cos(ang_zx)};

    // A convenient light direction for the purpose of this demo is to point at the center of the bounding
    // sphere. Then x_max = r, and x_min = -r. Same for y_max and y_min.

    // Vector3 pos = center + Vector3{radius, radius, radius};
    g_dir_lights.emplace_back(
        pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.4f, 0.4f, 0.4f), Vector3(0.0f, 0.0f, 0.0f));

    // Second quadrant
    Matrix4x4 rotation = rotation_about_y(pi / 2.0f);
    pos = Vector3(rotation * Vector4(pos, 1.0f));
    g_dir_lights.emplace_back(
        pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.9f, 0.9f, 0.9f), Vector3(0.0f, 0.0f, 0.0f));

    // Third quadrant
    rotation_about_y_update(rotation, 2 * pi / 2.0f);
    pos = Vector3(rotation * Vector4(pos, 1.0f));
    g_dir_lights.emplace_back(
        pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.2f, 0.2f, 0.2f), Vector3(0.0f, 0.0f, 0.0f));

    // Fourth quadrant
    rotation_about_y_update(rotation, 3 * pi / 2.0f);
    pos = Vector3(rotation * Vector4(pos, 1.0f));
    g_dir_lights.emplace_back(
        pos, center - pos, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.2f, 0.2f, 0.2f), Vector3(0.0f, 0.0f, 0.0f));

    CHECK_F(demo_constants::light_count == g_dir_lights.size(),
            "demo_constants::light_count = %u, g_dir_lights.size() = %zu",
            demo_constants::light_count,
            g_dir_lights.size());
}

Fn set_up_casting_light_gizmo(App &app) {
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

Fn init_rasterizer_states(App &app) {
    RasterizerStateDesc rs = default_rasterizer_state_desc;
    app.rasterizer_states.no_shadows = default_binding_state().add_rasterizer_state(rs);

    rs = default_rasterizer_state_desc;
    rs.slope_scaled_depth_bias = 3.0f;
    rs.constant_depth_bias = 4;
    app.rasterizer_states.first_pass = default_binding_state().add_rasterizer_state(rs);

    rs = default_rasterizer_state_desc;
    app.rasterizer_states.second_pass = default_binding_state().add_rasterizer_state(rs);
}

// ------------------------
//
// Render without shadows
// ------------------------
Fn render_no_shadow(App &app) {
    default_binding_state().set_rasterizer_state(app.rasterizer_states.no_shadows);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClearColor(XYZW(colors::AliceBlue));

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // Source the main camera's eye block.
    glInvalidateBufferData(app.ublock_bindings.eye_block.handle());

    if (app.mode == Mode::NO_SHADOWS) {
        source_eye_block_uniform(app, app.eye_block);
    } else if (app.mode == Mode::LIGHT_VIEW) {
        source_eye_block_uniform(app, app.shadow_map.eye_block);
        glBufferSubData(GL_UNIFORM_BUFFER,
                        offsetof(uniform_formats::EyeBlock, eye_pos),
                        sizeof(Vector3),
                        &app.eye_block.eye_pos);
    } else {
        CHECK_F(false);
    }

    if (1) {
        glUseProgram(app.shader_programs.no_shadow);

        for (const RenderableData &rd : app.opaque_renderables) {
            source_per_object_uniforms(app, rd);
            glBindVertexArray(rd.vao);
            glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
            glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
            glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (1) {
        glUseProgram(app.shader_programs.no_lights);
        if (0) {
            // Draw the scene bounding sphere
            auto &rd = app.rd_scene_bs;
            source_per_object_uniforms(app, rd);
            glBindVertexArray(rd.vao);
            glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
            glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
            glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
        }
    }

    {
        // Draw the light box
        glUseProgram(app.shader_programs.structured_textured_use);

        auto &rd = app.rd_light_box;
        source_per_object_uniforms(app, rd);
        glBindVertexArray(rd.vao);
        glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
        glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
        glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
    };

    // Draw casting light gizmo
    if (1) {
        glUseProgram(app.shader_programs.no_lights);
        auto &rd = app.rd_casting_light;
        source_per_object_uniforms(app, rd);
        glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
        glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
        glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
        glLineWidth(1.0f);
    }

    // Draw light xyz and sphere xyz axes
    glDisable(GL_DEPTH_TEST);
    glUseProgram(app.shader_programs.no_lights);
    {
        auto &rd = app.rd_light_xyz;
        source_per_object_uniforms(app, rd);
        glBindVertexArray(rd.vao);
        glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
        glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
        glDrawArrays(GL_LINES, 0, 6);
    }
    {
        auto &rd = app.rd_sphere_axes;
        source_per_object_uniforms(app, rd);
        glBindVertexArray(rd.vao);
        glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
        glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
        glDrawArrays(GL_LINES, 0, 6);
    }
    glEnable(GL_DEPTH_TEST);

    glfwSwapBuffers(app.window);
}

void init_shadow_map(App &app) {
    shadow_map::InitInfo init_info;
    init_info.light_direction = g_dir_lights[0].direction;
    LOG_F(INFO, "dir_lights[0].direction = [%f, %f, %f]", XYZ(g_dir_lights[0].direction));
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

        app.rd_light_xyz.uniforms.world_from_local_xform =
            world_from_light_xform *
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
            xyz_scale_matrix(Vector3{init_info.x_extent, init_info.y_extent, init_info.neg_z_extent});
        translate_update(rd.uniforms.world_from_local_xform, {0.0f, 0.0f, -init_info.neg_z_extent * 0.5f});
        rd.uniforms.world_from_local_xform = world_from_light_xform * rd.uniforms.world_from_local_xform;

        rd.uniforms.inv_world_from_local_xform = inverse(rd.uniforms.world_from_local_xform);
        rd.uniforms.material = PINK_MATERIAL;
        rd.num_indices = app.stripped_meshes.cube.num_faces * 3;
        rd.packed_attr_size = app.stripped_meshes.cube.packed_attr_size;
    }

    {
        const auto m = inverse_rotation_translation(light_from_world_xform(app.shadow_map));
        const auto r = app.scene_bounding_sphere.radius;
        const auto c = app.scene_bounding_sphere.center;
        Vector3 sphere_touch_point = c + m.y * r;
        // Transform touch point to light coords
        const auto wrt_light = light_from_world_xform(app.shadow_map) * Vector4(sphere_touch_point, 1.0f);
        const auto wrt_ndc = clip_from_light_xform(app.shadow_map) * wrt_light;
        LOG_F(INFO, "Touch point toward y wrt light [%f, %f, %f, %f]", XYZW(wrt_light));
        LOG_F(INFO, "Touch point toward y wrt ndc [%f, %f, %f, %f]", XYZW(wrt_ndc));
    }
}

// -----
//
// Renders to the depth map from the point of view of the casting light
// -----
void render_to_depth_map(App &app) {
    glViewport(0, 0, app.shadow_map.texture_size, app.shadow_map.texture_size);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);

    default_binding_state().set_rasterizer_state(app.rasterizer_states.first_pass);

    // Set the depth buffer as current render target, and clear it.
    shadow_map::set_as_draw_fbo(app.shadow_map);
    shadow_map::clear(app.shadow_map);

    // Set the eye transforms
    source_eye_block_uniform(app, app.shadow_map.eye_block);

    glUseProgram(app.shader_programs.build_depth_map);

    // Render all opaque objects
    {
        for (const RenderableData &rd : app.opaque_renderables) {
            source_per_object_uniforms(app, rd);
            glBindVertexArray(rd.vao);
            glBindBuffer(GL_ARRAY_BUFFER, rd.vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
            glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
            glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
        }
    }

    // Restore default framebuffer
    shadow_map::set_as_read_fbo(app.shadow_map);
}

// -----------
//
// Visualize the depth map by blitting it to the screen
// -----------
void blit_depth_map_to_screen(App &app) {
    glViewport(0, 0, demo_constants::window_width, demo_constants::window_height);

    glUseProgram(app.shader_programs.blit_depth_map);
    glDisable(GL_DEPTH_TEST);

    default_binding_state().set_rasterizer_state(app.rasterizer_states.first_pass);

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

Fn render_second_pass(App &app) {
    default_binding_state().set_rasterizer_state(app.rasterizer_states.first_pass);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClearColor(XYZW(colors::Cornsilk));
    glViewport(0, 0, demo_constants::window_width, demo_constants::window_height);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shadow_map::bind_comparing_sampler(app.shadow_map);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // Source the main camera's eye block.
    glInvalidateBufferData(app.ublock_bindings.eye_block.handle());
    source_eye_block_uniform(app, app.eye_block);

    {
        // Only difference with render_no_shadow is that we use the other shader program. Factor this out.
        glUseProgram(app.shader_programs.with_shadow);

        for (const RenderableData &rd : app.opaque_renderables) {
            source_per_object_uniforms(app, rd);
            glBindVertexArray(rd.vao);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.ebo);
            glBindVertexBuffer(0, rd.vbo, 0, rd.packed_attr_size);
            glDrawElements(GL_TRIANGLES, rd.num_indices, GL_UNSIGNED_SHORT, 0);
        }
    }
}

void render_with_shadow(App &app) {
    render_to_depth_map(app);
    render_second_pass(app);
    glfwSwapBuffers(app.window);
}

void render_depth_map(App &app) {
    render_to_depth_map(app);
    blit_depth_map_to_screen(app);
    glfwSwapBuffers(app.window);
}

namespace app_loop {

static eng::StartGLParams start_params;

template <> void init(App &app) {
    auto t0 = std::chrono::high_resolution_clock::now();

    start_params.window_width = demo_constants::window_width;
    start_params.window_height = demo_constants::window_height;
    start_params.window_title = "shadow map playground";
    start_params.major_version = 4;
    start_params.minor_version = 5;
    start_params.abort_on_error = true;
    app.window = eng::start_gl(start_params);
    eng::init_gl_globals();

    app.window_should_close = false;
    app.set_input_handler(input::make_handler<BasicInputHandler>(app));

    REGISTER_GLFW_CALLBACKS(app, app.window);

    build_geometry_buffers_and_bounding_sphere(app);
    load_structured_texture(app);
    set_up_lights(app);
    set_up_materials(app);
    create_uniform_buffers(app);
    init_shadow_map(app);
    init_uniform_data(app);
    set_up_camera(app);
    set_up_casting_light_gizmo(app);

    init_rasterizer_states(app);

    init_shader_defines(app);

    load_no_shadow_program(app);
    load_no_lights_program(app);
    load_shadow_map_programs(app);

    app.camera.move_upward(10.0f);
    app.camera.move_forward(-20.0f);

    auto t1 = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::ratio<1, 1000>> startup_time(t1 - t0);

    LOG_F(INFO, "Took %.2f ms to render first frame", startup_time.count());

    // app.camera.look_at(g_dir_lights[0].position);

    LOG_F(INFO, "Number of opaque renderables = %zu", g_scene_objects.size());
}

template <> void update(App &app, State &state) {
    glfwPollEvents();

    if (app.camera.needs_update() ||
        eng::handle_eye_input(
            app.window, app.camera._eye, state.frame_time_in_sec, app.camera._view_xform)) {

        app.camera.update_view_transform();
        update_camera_eye_block(app, state.frame_time_in_sec);
        app.camera.set_needs_update(false);
    }
}

template <> void render(App &app) {
    static int frame_count = 0;

#if 1
    if (app.mode == Mode::NO_SHADOWS) {
        render_no_shadow(app);
    } else if (app.mode == Mode::LIGHT_VIEW) {
        render_no_shadow(app);
    } else if (app.mode == Mode::DEPTH_VIEW) {
        render_depth_map(app);
    } else if (app.mode == Mode::WITH_SHADOWS) {
        render_with_shadow(app);
    } else {
        CHECK_F(false);
    }
#else
    render_with_shadow(app);
#endif
}

template <> void close(App &app) {
    eng::close_gl_globals();
    eng::close_gl(start_params);
    glfwTerminate();
}

template <> bool should_close(App &app) {
    return app.window_should_close || glfwWindowShouldClose(app.window);
}

} // namespace app_loop

int main() {
    eng::init_non_gl_globals();
    DEFERSTAT(eng::close_non_gl_globals());

    App app;
    app_loop::State loop_state{};
    app_loop::run(app, loop_state);
}
