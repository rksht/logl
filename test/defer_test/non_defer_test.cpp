// A non-deferred version first.

#include "essentials.h"

#include <learnogl/dds_loader.h>
#include <scaffold/string_stream.h>

#include <cxxopts.hpp>
#include <loguru.hpp>

using namespace fo;
using namespace math;

static bool confirm_enough_ub_space(size_t num_bars) {
    GLint max_bytes = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_bytes);

    const GLint required = num_bars * sizeof(BarWorldTransform);

    if (required > max_bytes) {
        log_err("Max uniform block size = %i bytes, but require %i bytes for %zu bars", max_bytes, required,
                num_bars);
        return false;
    }
    return true;
}

class App {
  public:
    App(std::vector<BarWorldTransform> bar_world_transforms, std::string normal_map_file,
        const std::string &material_file, std::vector<Light> lights);
    ~App();

    MVP mvp;
    GLuint mvp_ubo;

    std::vector<BarWorldTransform> bar_world_transforms;
    GLuint bar_ubo;

    GLuint bar_vao, bar_vbo, bar_ebo;
    unsigned num_indices;

    std::vector<Light> light_properties;
    GLuint light_properties_ubo;

    Material bar_material;
    GLuint bar_material_ubo;

    GLuint light_sphere_vao;
    GLuint light_spheres_ubo;
    unsigned num_light_sphere_indices;

    eye::State eye;

    GLuint non_deferred_prog; // For the non deferred shading
    GLuint light_sphere_prog; // For drawing the light spheres

    GLFWwindow *window;

    std::string normal_map_file;

    bool esc_pressed = false;

    size_t num_bars() const { return bar_world_transforms.size(); }
    size_t num_point_lights() const { return light_properties.size(); }
};

// Impl App::App
App::App(std::vector<BarWorldTransform> bar_world_transforms, std::string normal_map_file,
         const std::string &material_file, std::vector<Light> lights)
    : bar_world_transforms(std::move(bar_world_transforms))
    , normal_map_file(std::move(normal_map_file))
    , light_properties(std::move(lights)) {

// Initialize the material and lights here
#if 0
    Light light = {
        {0.95f, 0.95f, 0.95f}, // Strength
        1.4f,                  // Falloff start
        -unit_x,               // direction
        3.5f,                  // Falloff end
        {0.0f, 0.0f, 0.0f},    // Position
        0                      // Not a spot light
    };

    light_properties = {light};
#endif

    parse_material(bar_material, material_file);
}

App::~App() {}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    App &app = *(App *)glfwGetWindowUserPointer(window);
}

static void make_lightspheres_vao_and_ubo(App &app) {
    // vao for the vertices
    {
        auto sphere = par_shapes_create_parametric_sphere(10, 10);
        shift_par_cube(sphere);

        app.num_light_sphere_indices = 3 * sphere->ntriangles;

        GLuint vbo, ebo;
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glGenVertexArrays(1, &app.light_sphere_vao);
        glBindVertexArray(app.light_sphere_vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 3 * sizeof(float) * sphere->npoints, sphere->points, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * app.num_light_sphere_indices,
                     sphere->triangles, GL_STATIC_DRAW);
        glVertexBindingDivisor(0, 0);

        constexpr int k_pos_loc = 0;
        glEnableVertexAttribArray(k_pos_loc);
        glVertexAttribBinding(k_pos_loc, 0);
        glVertexAttribFormat(k_pos_loc, 3, GL_FLOAT, GL_FALSE, (GLsizeiptr)0);
        glBindVertexBuffer(0, vbo, 0, 3 * sizeof(float));

        par_shapes_free_mesh(sphere);
    }
    // ubo for transforms
    {
        glGenBuffers(1, &app.light_spheres_ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, app.light_spheres_ubo);

        std::vector<Matrix4x4> light_sphere_transforms;
        light_sphere_transforms.reserve(app.light_properties.size());

        for (const auto &l : app.light_properties) {
            light_sphere_transforms.push_back(identity_matrix);
            auto &m = light_sphere_transforms.back();
            m = xyz_scale_matrix(l.falloff_end, l.falloff_end, l.falloff_end) * m;
            translate_update(m, l.position);
        }
        glBufferData(GL_UNIFORM_BUFFER, vec_bytes(light_sphere_transforms),
                     light_sphere_transforms.data(), GL_STATIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 4, app.light_spheres_ubo);
    }
}

static std::string get_shader_defines(const App &app) {
    using std::make_pair;
    std::vector<std::pair<const char *, int>> defines{
        make_pair("ACTUALLY_USING", 1),
        make_pair("K_NUM_BARS", (int)app.num_bars()),
        make_pair("NUM_POINT_LIGHTS", (int)app.num_point_lights()),
        make_pair("NORMAL_MAP_BINDING", 1),
        make_pair("MVP_UBO_BINDING", 0),
        make_pair("BAR_TRANSFORMS_UBO_BINDING", 1),
        make_pair("BAR_MATERIAL_UBO_BINDING", 2),
        make_pair("LIGHT_PROPERTIES_UBO_BINDING", 3),
        make_pair("LIGHTSPHERES_UBO_BINDING", 4)};

    std::string s;
    for (const auto &p : defines) {
        s += "#define ";
        s += p.first;
        s += " ";
        s += std::to_string(p.second);
        s += "\n";
    }
    return s;
}

static GLuint create_vertex_shader(const App &app, fs::path vertex_shader_file) {
    Array<uint8_t> source(memory_globals::default_allocator());
    read_file(vertex_shader_file, source, true);
    const auto defines = get_shader_defines(app);
    return eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                                  GL_VERTEX_SHADER, defines.c_str(), source);
}

static GLuint create_fragment_shader(const App &app, fs::path fragment_shader_file) {
    Array<uint8_t> source(memory_globals::default_allocator());
    read_file(fragment_shader_file, source, true);
    const auto defines = get_shader_defines(app);
    return eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                                  GL_FRAGMENT_SHADER, defines.c_str(), source);
}

static GLuint create_light_sphere_prog(const App &app) {
    constexpr auto lightsphere_vs = R"(
        #version 430 core

        layout(location = 0) in vec3 position;

        layout(binding = MVP_UBO_BINDING, std140) uniform MVP {
            mat4 view;
            mat4 proj;
            vec4 eyepos_world;
        } mvp;

        layout(binding = LIGHTSPHERES_UBO_BINDING, std140) uniform LightSphereTransforms {
            mat4 arr_light_sphere_transform[NUM_POINT_LIGHTS];
        };

        void main() {
            gl_Position = mvp.proj * mvp.view * arr_light_sphere_transform[gl_InstanceID] * vec4(position, 1.0);
        }
    )";

    constexpr auto lightsphere_fs = R"(
        #version 430 core
    
        out vec4 frag_color;
    
        #define SPHERE_COLOR 1.0, 1.0, 1.0
        #define SPHERE_ALPHA 0.4
    
        void main() {
            frag_color = vec4(SPHERE_COLOR, SPHERE_ALPHA);
        }
    )";

    GLuint vs =
        eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                               GL_VERTEX_SHADER, get_shader_defines(app).c_str(), lightsphere_vs);
    GLuint fs = eng::create_shader({}, nullptr, GL_FRAGMENT_SHADER, lightsphere_fs);
    return eng::create_program(vs, fs);
}

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, k_window_width, k_window_height, "non_defer", 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    CHECK_F(confirm_enough_ub_space(app.bar_world_transforms.size()), "Not enough uniform block space");

    // Create programs
    app.non_deferred_prog =
        eng::create_program(create_vertex_shader(app, fs::path(SOURCE_DIR) / "vs.vert"),
                                create_fragment_shader(app, fs::path(SOURCE_DIR) / "lighting.frag"));
    app.light_sphere_prog = create_light_sphere_prog(app);

    // Initialize MVP uniform
    app.mvp.proj = persp_proj(0.1, 1000.0, 70.0 * one_deg_in_rad, k_aspect_ratio);

    app.eye = eye::toward_negz(2.0f);
    eye::update_view_transform(app.eye, app.mvp.view);
    app.mvp.eyepos_world = Vector4(app.eye.position, 1.0f);

    // Create and initialize mvp ubo
    static_assert(sizeof(MVP) == 2 * sizeof(Matrix4x4) + sizeof(Vector4), "");
    glGenBuffers(1, &app.mvp_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.mvp_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(MVP), &app.mvp, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, app.mvp_ubo);

    make_bar_vao(&app.bar_vao, &app.num_indices);
    make_bar_transforms_ubo(&app.bar_ubo, app.bar_world_transforms);
    make_light_material_ubo(&app.bar_material_ubo, app.bar_material, &app.light_properties_ubo,
                            app.light_properties);
    make_lightspheres_vao_and_ubo(app);

    // Create normal map texture
    glActiveTexture(GL_TEXTURE0 + 1);
    GLuint normal_map_texture =
        dds::load_texture(app.normal_map_file, 0, nullptr, &memory_globals::default_allocator());

    // Set some basic state
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.f, 0.f, 0.f, 1.0f);

    glEnable(GL_BLEND); // For visualising the light spheres
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetKeyCallback(app.window, key_callback);
}

template <> void update<App>(App &app, State &s) {
    fps_title_update(app.window, s.frame_time_in_sec);
    glfwPollEvents();

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
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#if 1
    glUseProgram(app.non_deferred_prog);
    glBindVertexArray(app.bar_vao);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDrawElementsInstanced(GL_TRIANGLES, app.num_indices, GL_UNSIGNED_SHORT, 0, app.num_bars());
#endif

#if 0
    // Draw the light spheres
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(app.light_sphere_prog);
    glBindVertexArray(app.light_sphere_vao);
    glDrawElementsInstanced(GL_TRIANGLES, app.num_light_sphere_indices, GL_UNSIGNED_SHORT, 0,
                            app.num_point_lights());
#endif

    glfwSwapBuffers(app.window);
}

template <> bool should_close<App>(App &app) { return app.esc_pressed || glfwWindowShouldClose(app.window); }

template <> void close<App>(App &app) {
    eng::close_gl();
    glfwTerminate();
}

} // namespace app_loop

int main(int ac, char **av) {
    memory_globals::init(1024);

    // clang-format off
    cxxopts::Options desc("Allowed options");
    desc.add_options()("help", "produce help message")
        ("barfile", cxxopts::value<std::string>(), "file containing bar world transforms")
        ("normalmap", cxxopts::value<std::string>(), "normal map")
        ("material", cxxopts::value<std::string>(), "Material description file (JSON)")
        ("lightfile", cxxopts::value<std::string>(), "Lights file");
    // clang-format on
    auto result = desc.parse(ac, av);

    CHECK_F(result.count("barfile") == 1, "Must give a file to read the bar data from");
    CHECK_F(result.count("normalmap") == 1, "Normal map file needs to be given");
    CHECK_F(result.count("material") == 1, "Material file not given");
    CHECK_F(result.count("lightfile") == 1, "Must give a file containing the lights");

    rng::init_rng();
    fps_title_init("Simple light");
    {
        auto bar_world_transforms = read_bar_transforms(result["barfile"].as<std::string>());
        auto lights = read_lights(result["lightfile"].as<std::string>());

        App app(std::move(bar_world_transforms), result["normalmap"].as<std::string>(),
                vm["material"].as<std::string>(), std::move(lights));
        app_loop::State app_state{};
        app_loop::run(app, app_state);
    }
    memory_globals::shutdown();
}
