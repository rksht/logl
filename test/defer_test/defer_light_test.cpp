// Only point lights

#include "essentials.h"

#include <argh.h>
#include <learnogl/dds_loader.h>
#include <loguru.hpp>

using namespace fo;
using namespace math;

// Textures used in the app
enum TextureBindings : GLuint {
    normal_map = 0,
    pos_and_normal = 1,
    depth = 2,
};

namespace gbuffer {

enum ColorAttachmentIndices : unsigned { pos_and_normal, _count };

struct FBInfo {
    GLuint fb;
    GLuint color_textures[ColorAttachmentIndices::_count];
    GLuint depth_texture;
};

static void init(FBInfo &g, int width, int height) {
    glGenTextures(ColorAttachmentIndices::_count, g.color_textures);
    glGenTextures(1, &g.depth_texture);

    glGenFramebuffers(1, &g.fb);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g.fb);

    // position + normal attachment, stored as half floats.
    glBindTexture(GL_TEXTURE_2D, g.color_textures[ColorAttachmentIndices::pos_and_normal]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB32UI, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER,
                         GL_COLOR_ATTACHMENT0 + ColorAttachmentIndices::pos_and_normal,
                         g.color_textures[ColorAttachmentIndices::pos_and_normal],
                         0);

    // Set draw color buffers for the fb
    std::array<GLenum, ColorAttachmentIndices::_count> drawbuffers{GL_COLOR_ATTACHMENT0};
    CHECK_EQ_F(drawbuffers.size(), ColorAttachmentIndices::_count);
    glDrawBuffers(drawbuffers.size(), drawbuffers.data());

    // Depth texture
    glBindTexture(GL_TEXTURE_2D, g.depth_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, width, height);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, g.depth_texture, 0);

    // Just in case
    CHECK_F(eng::framebuffer_complete(g.fb), "Framebuffer isn't complete");
    // Bind default fb as current write framebuffer
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

} // namespace gbuffer

auto build_light_spheres(const std::vector<Light> &lights) {
    std::vector<Vector4> scale_and_trans; // xyz = translate, w = scale
    for (const auto &light : lights) {
        scale_and_trans.emplace_back(light.position, light.falloff_end);
    }
    return scale_and_trans;
}

struct InitData {
    std::vector<BarWorldTransform> bar_world_transforms;
    std::vector<Light> lights;
    std::vector<Sphere> light_spheres;
    std::string normal_map_file;
    Quad quad = {{-1.0f, -1.0f}, {1.0f, 1.0f}};
    Material bar_material;
};

struct App {
    App(std::vector<BarWorldTransform> bar_world_transforms_,
        std::vector<Light> lights_,
        const std::string &normal_map_file,
        const std::string &material_file) {
        init_data = MAKE_NEW(memory_globals::default_scratch_allocator(), InitData);
        init_data->bar_world_transforms = std::move(bar_world_transforms_);
        init_data->lights = std::move(lights_);
        init_data->normal_map_file = normal_map_file;

        parse_material(init_data->bar_material, material_file);

        this->num_bars = init_data->bar_world_transforms.size();
        this->num_point_lights = init_data->lights.size();
    }

    gbuffer::FBInfo g;

    GLuint num_bars;
    GLuint num_bar_mesh_indices;
    GLuint num_point_lights;

    GLuint bar_vao;
    GLuint quad_vao;

    GLuint bar_transforms_ubo;
    GLuint light_properties_ubo;
    GLuint light_sphere_info_ubo;

    GLuint geom_pass_program;
    GLuint light_pass_program;
    GLuint geom_pass_fbo;

    MVP mvp;
    eye::State eye;
    GLuint mvp_ubo;

    Material bar_material;
    GLuint bar_material_ubo;
    GLuint normal_map_texture;

    InitData *init_data;

    GLuint calculate_eye_frame_uniformloc;
    bool calculate_in_eye_frame = true;
    bool calculate_changed = true;

    GLFWwindow *window;
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

    // Integer defines
    {
        std::vector<std::pair<const char *, int>> defines{
            make_pair("ACTUALLY_USING", 1),
            make_pair("K_NUM_BARS", (int)app.num_bars),
            make_pair("NUM_POINT_LIGHTS", (int)app.num_point_lights),
            make_pair("NORMAL_MAP_BINDING", TextureBindings::normal_map),
            make_pair("POS_AND_NORMAL_TEXTURE_BINDING", TextureBindings::pos_and_normal),
            make_pair("DEPTH_TEXTURE_BINDING", TextureBindings::depth),
            make_pair("MVP_UBO_BINDING", MVP_UBO_BINDING),
            make_pair("BAR_TRANSFORMS_UBO_BINDING", BAR_TRANSFORMS_UBO_BINDING),
            make_pair("BAR_MATERIAL_UBO_BINDING", BAR_MATERIAL_UBO_BINDING),
            make_pair("LIGHT_PROPERTIES_UBO_BINDING", LIGHT_PROPERTIES_UBO_BINDING),
            make_pair("LIGHTSPHERES_UBO_BINDING", LIGHTSPHERES_UBO_BINDING),
            make_pair("POS_AND_NORMAL_ATTACHMENT", gbuffer::ColorAttachmentIndices::pos_and_normal)};
        push_defines(defines, s);
    }

    // Float defines
    {
        std::vector<std::pair<const char *, float>> defines{
            make_pair("Z_NEAR", 0.1f),
            make_pair("TAN_VFOV_DIV_2", std::tan(HFOV / 2.0f * one_deg_in_rad) / k_aspect_ratio),
            make_pair("ASPECT_RATIO", k_aspect_ratio)};
        push_defines(defines, s);
    }

    return s;
}

static void create_shader_programs(App &app) {
    const std::string defines = get_shader_defines(app);

    Array<uint8_t> src(memory_globals::default_allocator());
    read_file(fs::path(SOURCE_DIR) / "vs.vert", src);
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
}

static void begin_geom_pass(App &app) {
    glUseProgram(app.geom_pass_program);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, app.g.fb);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void begin_light_pass(App &app) {
    glUseProgram(app.light_pass_program);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, app.g.fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

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

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, k_window_width, k_window_height, "defer lights test", 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    app.mvp.proj = persp_proj(Z_NEAR, Z_FAR, HFOV * one_deg_in_rad, k_aspect_ratio);
    app.eye = eye::toward_negz(2.0f);
    eye::update_view_transform(app.eye, app.mvp.view);
    app.mvp.eyepos_world = Vector4(app.eye.position, 1.0f);
    // Create and initialize mvp ubo
    static_assert(sizeof(MVP) == 2 * sizeof(Matrix4x4) + sizeof(Vector4), "");
    glGenBuffers(1, &app.mvp_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.mvp_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(MVP), &app.mvp, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, app.mvp_ubo);

    // Make the bar vao
    make_bar_vao(&app.bar_vao, &app.num_bar_mesh_indices);

    // Make bar transforms ubo
    make_bar_transforms_ubo(&app.bar_transforms_ubo, app.init_data->bar_world_transforms);

    // Make light and material ubo
    make_light_material_ubo(
        &app.bar_material_ubo, app.bar_material, &app.light_properties_ubo, app.init_data->lights);

    // Set up quad vao
    GLuint quad_vbo;
    app.init_data->quad.make_vao(&quad_vbo, &app.quad_vao);

    // Load shaders
    create_shader_programs(app);

    // Set up the geometry buffer
    gbuffer::init(app.g, k_window_width, k_window_height);

    // Create and/or bind textures. One time is enough, since we do not change texture/binding associations.
    glActiveTexture(GL_TEXTURE0 + TextureBindings::normal_map);

    LOG_F(INFO, "Creating normal map");

#if 1
    GLuint normal_map_texture =
        dds::load_texture(app.init_data->normal_map_file, 0, nullptr, &memory_globals::default_allocator());

#else

    GLuint normal_map_texture = load_texture(app.init_data->normal_map_file.c_str(),
                                             TextureFormat{GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE},
                                             TextureBindings::normal_map);

#endif

    LOG_F(INFO, "Created normal map");

    glActiveTexture(GL_TEXTURE0 + TextureBindings::pos_and_normal);
    glBindTexture(GL_TEXTURE_2D, app.g.color_textures[gbuffer::ColorAttachmentIndices::pos_and_normal]);

    glActiveTexture(GL_TEXTURE0 + TextureBindings::depth);
    glBindTexture(GL_TEXTURE_2D, app.g.depth_texture);

    // Release unneeded data
    MAKE_DELETE(memory_globals::default_scratch_allocator(), InitData, app.init_data);
    app.init_data = nullptr;

    // For testing that eye vs world frame calculation difference. There shouldn't be any.
    app.calculate_eye_frame_uniformloc =
        glGetUniformLocation(app.light_pass_program, "calculate_in_eye_frame");

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetKeyCallback(app.window, key_callback);

    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

template <> void update<App>(App &app, State &s) {
    glfwPollEvents();

    // fps_title_update(app.window, s.frame_time_in_sec);

    if (app.calculate_changed) {
        glUseProgram(app.light_pass_program);
        glUniform1ui(app.calculate_eye_frame_uniformloc, app.calculate_in_eye_frame ? 1 : 0);
        app.calculate_changed = false;
    }

    if (eng::handle_eye_input(app.window, app.eye, s.frame_time_in_sec, app.mvp.view)) {
        app.mvp.eyepos_world = Vector4(app.eye.position, 1.0f);
        glBindBuffer(GL_UNIFORM_BUFFER, app.mvp_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MVP), &app.mvp);
    }

    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    begin_geom_pass(app);

    glBindVertexArray(app.bar_vao);
    glDrawElementsInstanced(GL_TRIANGLES, app.num_bar_mesh_indices, GL_UNSIGNED_SHORT, 0, app.num_bars);

    begin_light_pass(app);

    glBindVertexArray(app.quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glfwSwapBuffers(app.window);
}

template <> bool should_close<App>(App &app) { return app.esc_pressed || glfwWindowShouldClose(app.window); }

template <> void close<App>(App &app) {
    eng::close_gl();
    glfwTerminate();
}

} // namespace app_loop

int main(int ac, char **av) {
    memory_globals::init();

    argh::parser cmdl(av);

    rng::init_rng();
    fps_title_init("Deferred lighting");
    {
        std::string bar_file;
        cmdl("barfile", "") >> bar_file;

        LOG_F(INFO, "barfile = %s", bar_file.c_str());

        auto bar_world_transforms = read_bar_transforms(bar_file);

        std::string light_file;
        cmdl("lightfile", "") >> light_file;
        auto lights = read_lights(light_file);

        LOG_F(INFO, "lightfile = %s", light_file.c_str());

        std::string normal_map_file;
        cmdl("normalmap", "") >> normal_map_file;

        LOG_F(INFO, "normalmap = %s", normal_map_file.c_str());

        std::string material_file;
        cmdl("material", "") >> material_file;

        LOG_F(INFO, "material_file = %s", material_file.c_str());

        App app(std::move(bar_world_transforms), std::move(lights), normal_map_file, material_file);
        app_loop::State app_state{};
        app_loop::run(app, app_state);
    }
    memory_globals::shutdown();
}
