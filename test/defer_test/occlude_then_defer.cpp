// In this one I will use a compute shader to prevent the passage of occluded renderables into the pipeline
// altogether. That is, not only will they be prevented from being shaded by the pixel-shader (which is
// already handlede, either by early-z, or if not that geometry pass itself), but they will be prevented from
// being shaded by the vertex shader too.

// The idea is to use a compute shader, which runs on the GPU, to filter out the definitely-occuluded
// renderables before sending to the vertex shader.

// Only point lights

#include "essentials.h"

#include <argh.h>
#include <learnogl/dds_loader.h>
#include <loguru.hpp>
#include <signal.h>

struct CookTorranceMaterial {
    Vector3 roughness;
};

using namespace fo;
using namespace math;

static bool use_instanced = true;

// Textures used in the app
enum TextureBindings : GLuint {
    normal_map = 0,
    pos_and_normal = 1,
    depth = 2,
};

auto build_light_spheres(const std::vector<Light> &lights) {
    std::vector<Vector4> scale_and_trans; // xyz = translate, w = scale
    for (const auto &light : lights) {
        scale_and_trans.emplace_back(light.position, light.falloff_end);
    }
    return scale_and_trans;
}

struct InitData {
    Array<LocalTransform> sphere_xforms{memory_globals::default_allocator()};
    Array<Light> lights{memory_globals::default_allocator()};
    Array<Sphere> light_spheres{memory_globals::default_allocator()};
    std::string normal_map_file;
    Quad quad = {{-1.0f, -1.0f}, {1.0f, 1.0f}};
    Material sphere_material;
};

struct App {
    App(Array<LocalTransform> sphere_xforms_,
        Array<Light> lights_,
        const std::string &normal_map_file,
        const std::string &material_file) {
        init_data = make_new<InitData>(memory_globals::default_scratch_allocator());
        init_data->sphere_xforms = std::move(sphere_xforms_);
        init_data->lights = std::move(lights_);
        init_data->normal_map_file = normal_map_file;

        parse_material(init_data->sphere_material, material_file);

        this->num_spheres = size(init_data->sphere_xforms);
        this->num_point_lights = size(init_data->lights);
    }

    eng::GLApp gl;

    GLuint num_spheres;
    GLuint num_sphere_mesh_indices;
    GLuint num_point_lights;

    GLuint sphere_mesh_vao;
    GLuint quad_vao;

    GLuint sphere_xforms_ssbo;

    BoundUBO light_properties_ubo;
    BoundUBO light_spheres_ubo;

    GLuint geom_pass_program;
    GLuint light_pass_program;
    FBO geom_pass_fbo;
    GLuint geom_pass_rot_mat_uloc;
    GLuint geom_pass_object_number_uloc;

    BoundTexture normal_tex;

    BoundTexture pos_and_normal_tex;
    BoundTexture depth_texture;

    Matrix4x4 rotation_matrix;

    PerCameraUBlockFormat camera_ublock;
    eye::State eye;

    Material sphere_material;
    BoundUBO sphere_material_ubo;

    DebugInfoOverlay info_overlay;

    InitData *init_data;

    GLuint calculate_eye_frame_uniformloc;
    bool calculate_in_eye_frame = true;
    bool calculate_changed = true;

    bool esc_pressed = false;
};

template <typename T>
static void push_defines(const std::vector<std::pair<const char *, T>> &defines, std::string &s) {
    for (const auto &p : defines) {
        s += "#define ";
        s += p.first;
        s += " ";
        s += std::to_string(p.second);
        s += "\n";
    }
}

static std::string get_shader_defines(const App &app) {
    using std::make_pair;
    std::string s;

    ShaderDefines defs;

    defs.add("ACTUALLY_USING", 1)
        .add("NUM_SPHERES", (int)app.num_spheres)
        .add("NUM_POINT_LIGHTS", (int)app.num_point_lights)
        .add("NORMAL_MAP_BINDING", (int)app.normal_tex.point)
        .add("POS_AND_NORMAL_TEXTURE_BINDING", (int)app.pos_and_normal_tex.point)
        .add("DEPTH_TEXTURE_BINDING", (int)app.depth_texture.point)
        .add("VIEWPROJ_UBO_BINDING", (int)app.gl.bs.per_camera_ubo_binding())
        .add("SPHERE_XFORMS_SSBO_BINDING", SPHERE_XFORMS_SSBO_BINDING)
        .add("SPHERE_MATERIAL_UBO_BINDING", (int)app.sphere_material_ubo.point)
        .add("LIGHT_PROPERTIES_UBO_BINDING", (int)app.light_properties_ubo.point)
        .add("POS_AND_NORMAL_ATTACHMENT", 0)
        .add("Z_NEAR", 0.1f)
        .add("TAN_VFOV_DIV_2", std::tan(HFOV / 2.0f * one_deg_in_rad) / k_aspect_ratio)
        .add("ASPECT_RATIO", k_aspect_ratio)
        .add("INSTANCED", use_instanced ? 1 : 0);

    return defs.get_string();
}

static void load_shaders(App &app) {
    const std::string defines = get_shader_defines(app);

    LOG_F(INFO, "Defines = %s", defines.c_str());

    Array<uint8_t> src(memory_globals::default_allocator());
    read_file(fs::path(SOURCE_DIR) / "geom_vs.vert", src);
    auto vs = create_shader(CreateShaderOptionBits::k_prepend_version,
                            "#version 430 core\n",
                            GL_VERTEX_SHADER,
                            defines.c_str(),
                            (const char *)data(src));

    read_file(fs::path(SOURCE_DIR) / "geom_pass.frag", src);
    auto fs = create_shader(CreateShaderOptionBits::k_prepend_version,
                            "#version 430 core\n",
                            GL_FRAGMENT_SHADER,
                            defines.c_str(),
                            (const char *)data(src));

    app.geom_pass_program = eng::create_program(vs, fs);

    app.geom_pass_rot_mat_uloc = glGetUniformLocation(app.geom_pass_program, "rotation_matrix");
    app.geom_pass_object_number_uloc = glGetUniformLocation(app.geom_pass_program, "object_number");

    read_file(fs::path(SOURCE_DIR) / "quad_vs.vert", src);
    vs = create_shader(CreateShaderOptionBits::k_prepend_version,
                       "#version 430 core\n",
                       GL_VERTEX_SHADER,
                       defines.c_str(),
                       (const char *)data(src));

    read_file(fs::path(SOURCE_DIR) / "quad_with_lighting.frag", src);
    fs = create_shader(CreateShaderOptionBits::k_prepend_version,
                       "#version 430 core\n",
                       GL_FRAGMENT_SHADER,
                       defines.c_str(),
                       (const char *)data(src));

    app.light_pass_program = eng::create_program(vs, fs);

    // For testing that eye vs world frame calculation difference. There shouldn't be any.
    app.calculate_eye_frame_uniformloc =
        glGetUniformLocation(app.light_pass_program, "calculate_in_eye_frame");
}

static void begin_geom_pass(App &app) {
    glUseProgram(app.geom_pass_program);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    app.geom_pass_fbo.bind_as_writable();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void begin_light_pass(App &app) {
    glUseProgram(app.light_pass_program);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    app.geom_pass_fbo.bind_as_readable();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    App &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_E && action == GLFW_RELEASE) {
        app.calculate_in_eye_frame = !app.calculate_in_eye_frame;

        if (app.calculate_in_eye_frame) {
            LOG_F(INFO, "Calculating light in eye frame");
        } else {
            LOG_F(INFO, "Calculating light in world frame");
        }

        app.calculate_changed = true;
    }
}

void allot_textures(App &app) {
    {
        GLuint handle = dds::load_texture(
            app.init_data->normal_map_file, 0, nullptr, &memory_globals::default_allocator());

        app.normal_tex.texture = gl_desc::SampledTexture(handle);
        app.normal_tex.point = app.gl.bs.bind_unique(app.normal_tex.texture);
    }

    {
        GLuint handle;
        glGenTextures(1, &handle);
        app.pos_and_normal_tex.texture = gl_desc::SampledTexture(handle);
        app.pos_and_normal_tex.point = app.gl.bs.bind_unique(app.pos_and_normal_tex.texture);
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32UI, k_window_width, k_window_height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }

    {
        GLuint handle;
        glGenTextures(1, &handle);
        app.depth_texture.texture = gl_desc::SampledTexture(handle);
        app.depth_texture.point = app.gl.bs.bind_unique(app.depth_texture.texture);
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, k_window_width, k_window_height);
    }
}

void init_gbuffer(App &app) {
    app.geom_pass_fbo.gen()
        .bind_as_writable()
        .add_attachment(0, app.pos_and_normal_tex.texture.handle())
        .add_depth_attachment(app.depth_texture.texture.handle())
        .set_done_creating()
        .set_draw_buffers({0})
        .bind_as_readable();
}

void load_mesh_buffers(App &app) {
    // Make the bar vao
    app.sphere_mesh_vao = make_sphere_mesh_vao(&app.num_sphere_mesh_indices);

    // Make bar transforms ubo
    app.sphere_xforms_ssbo =
        make_sphere_xforms_ssbo(data(app.init_data->sphere_xforms), size(app.init_data->sphere_xforms));

    LOG_F(INFO, "size of transforms ssbo = %zu bytes", vec_bytes(app.init_data->sphere_xforms));

    // Make light and material ubo
    make_light_material_ubo(app.gl.bs,
        app.sphere_material_ubo, app.sphere_material, app.light_properties_ubo, app.init_data->lights);

    // Set up quad vao
    GLuint quad_vbo;
    app.init_data->quad.make_vao(&quad_vbo, &app.quad_vao);
}

namespace app_loop {

eng::StartGLParams start_gl_params;

template <> void init<App>(App &app) {
    start_gl_params.window_width = k_window_width;
    start_gl_params.window_height = k_window_height;
    start_gl_params.window_title = "defer lights test";
    start_gl_params.major_version = 4;
    start_gl_params.minor_version = 5;
    eng::start_gl(start_gl_params, app.gl);
    eng::enable_debug_output(nullptr, nullptr, true);

    // eng::init_gl_globals();

    app.camera_ublock.clip_from_view_xform = persp_proj(Z_NEAR, Z_FAR, HFOV * one_deg_in_rad, k_aspect_ratio);
    app.eye = eye::toward_negz(2.0f);
    eye::update_view_transform(app.eye, app.camera_ublock.view_from_world_xform);
    app.camera_ublock.eye_position = Vector4(app.eye.position, 1.0f);

    load_mesh_buffers(app);

    allot_textures(app);
    init_gbuffer(app);

    make_delete(memory_globals::default_scratch_allocator(), app.init_data);
    app.init_data = nullptr;

    app.info_overlay.init(app.gl.bs, k_window_width, k_window_height, colors::IndianRed, colors::FloralWhite);

    load_shaders(app);

    glfwSetWindowUserPointer(app.gl.window, &app);
    glfwSetKeyCallback(app.gl.window, key_callback);

    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    LOG_F(INFO, "Init complete");
}

template <> void update<App>(App &app, State &s) {
    glfwPollEvents();

    static float accum_angle = 0.0f;
    const float ang_velocity = 30.0f;
    accum_angle += s.frame_time_in_sec * ang_velocity;
    app.rotation_matrix = rotation_about_y(accum_angle);

    static double secs_per_frame = 0.0;
    secs_per_frame = 0.8 * secs_per_frame + 0.2 * s.frame_time_in_sec;
    double fps = 1.0 / secs_per_frame;
    app.info_overlay.write_string(fmt::format("fps = {}", std::floor(fps + 0.5)).c_str());

    if (app.calculate_changed) {
        glUseProgram(app.light_pass_program);
        glUniform1ui(app.calculate_eye_frame_uniformloc, app.calculate_in_eye_frame ? 1 : 0);
        app.calculate_changed = false;
    }

#if 1
    if (eng::handle_eye_input(
            app.gl.window, app.eye, s.frame_time_in_sec, app.camera_ublock.view_from_world_xform)) {
        app.camera_ublock.eye_position = Vector4(app.eye.position, 1.0f);
        // glBindBuffer(GL_UNIFORM_BUFFER, app.gl.bs.per_camera_ubo());
        // glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerCameraUBlockFormat), &app.camera_ublock);
    }
#endif

    if (glfwGetKey(app.gl.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    begin_geom_pass(app);

    // app.camera_ublock.eye_position = Vector4(app.eye.position, 1.0f);
    glBindBuffer(GL_UNIFORM_BUFFER, app.gl.bs.per_camera_ubo());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerCameraUBlockFormat), &app.camera_ublock);

    glUniformMatrix4fv(app.geom_pass_rot_mat_uloc, 1, GL_FALSE, (const float *)&app.rotation_matrix);
    glBindVertexArray(app.sphere_mesh_vao);

    if (use_instanced) {
        glDrawElementsInstanced(
            GL_TRIANGLES, app.num_sphere_mesh_indices, GL_UNSIGNED_SHORT, 0, app.num_spheres);
    } else {
        for (int i = 0; i < app.num_spheres; ++i) {
            glUniform1i(app.geom_pass_object_number_uloc, i);
            glDrawElements(GL_TRIANGLES, app.num_sphere_mesh_indices, GL_UNSIGNED_SHORT, (const void *)0);
        }
    }

    begin_light_pass(app);

    glBindVertexArray(app.quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    app.info_overlay.draw(app.gl.bs);

    glfwSwapBuffers(app.gl.window);
}

template <> bool should_close<App>(App &app) { return app.esc_pressed || glfwWindowShouldClose(app.gl.window); }

template <> void close<App>(App &app) {
    // eng::close_gl_globals();
    eng::close_gl(start_gl_params);
    glfwTerminate();
}

} // namespace app_loop

int main(int ac, char **av) {
    eng::init_non_gl_globals();
    DEFERSTAT(eng::close_non_gl_globals());

    argh::parser cmdl(av);

    std::string spheres_file;
    std::string lights_file;
    std::string normal_map_file;
    std::string material_file;

    CHECK_F(bool(cmdl("spheres") >> spheres_file), "Missing argument --spheres=<spheres file>");
    CHECK_F(bool(cmdl("lights") >> lights_file), "Missing argument --lights=<light file>");
    CHECK_F(bool(cmdl("normalmap") >> normal_map_file), "Missing argument --normalmap=<normalmap file>");
    CHECK_F(bool(cmdl("material") >> material_file), "Missing argument --material=<material file>");

    if (cmdl["noinstance"]) {
        use_instanced = false;
    } else {
        use_instanced = true;
    }

    LOG_F(INFO, "Using instanced rendering? %s", use_instanced ? "Yes" : "No");

    auto sphere_xforms = read_sphere_transforms(spheres_file);
    auto lights = read_lights(lights_file);

    App app(std::move(sphere_xforms), std::move(lights), normal_map_file, material_file);
    app_loop::State app_state{};
    app_loop::run(app, app_state);
}
