// A normal map viewer

#include "essentials.h"

#include <assert.h>
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cxxopts.hpp>
#include <learnogl/dds_loader.h>
#include <learnogl/math_ops.h>
#include <learnogl/mesh.h>
#include <scaffold/debug.h>
#include <vector>

#ifndef MODEL_FILE
#error "MODEL_FILE must be defined"
#endif

using namespace fo;
using namespace math;

struct Quad {
    struct VertexData {
        Vector3 position;
        Vector2 st;
    };
    VertexData vertices[6];

    GLuint vbo, vao;

    Quad() = default;

    Quad(Vector2 min, Vector2 max, float z = 0.4) {
        const Vector2 topleft = {min.x, max.y};
        const Vector2 bottomright = {max.x, min.y};

        auto set = [z](Vector3 &v, const Vector2 &corner) {
            v.x = corner.x;
            v.y = corner.y;
            v.z = z;
        };

        set(vertices[0].position, min);
        set(vertices[1].position, max);
        set(vertices[2].position, topleft);

        set(vertices[3].position, min);
        set(vertices[4].position, bottomright);
        set(vertices[5].position, max);

        vertices[0].st = Vector2{0, 0};
        vertices[1].st = Vector2{1, 1};
        vertices[2].st = Vector2{0, 1};

        vertices[3].st = Vector2{0, 0};
        vertices[4].st = Vector2{1, 0};
        vertices[5].st = Vector2{1, 1};
    }

    void make_vao() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, ARRAY_SIZE(vertices) * sizeof(VertexData), vertices, GL_STATIC_DRAW);

        glBindVertexBuffer(0, vbo, 0, sizeof(VertexData));
        glVertexAttribBinding(0, 0); // pos
        glVertexAttribBinding(1, 0); // st
        glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, position));
        glVertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, offsetof(VertexData, st));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }
};

static constexpr auto vs_quad = R"(
    #version 430 core

    layout(location = 0) in vec3 position;
    layout(location = 1) in vec2 st;

    out vec2 frag_st;

    void main() {
        gl_Position = vec4(position.xy, 1.0, 1.0);
        frag_st = st;
    }
)";

static constexpr auto fs_quad = R"(
    #version 430 core
    
    in vec2 frag_st;

    layout(binding = 0) uniform sampler2D normal_sampler;

    out vec4 frag_color;

    void main() {
        vec3 normal = texture(normal_sampler, frag_st).xyz;
        frag_color = vec4(normalize(normal.xyz), 1.0);
    }
)";

#include <learnogl/app_loop.h>
#include <learnogl/gl_misc.h>
#include <learnogl/math_ops.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <learnogl/stb_image_write.h>

constexpr int k_screen_width = 400;
constexpr int k_screen_height = 400;

static const char *g_image_file = nullptr;

struct MVP {
    Matrix4x4 model_mat = identity_matrix;
    Matrix4x4 view_mat;
    Matrix4x4 proj_mat;
};

struct App {
    App(fs::path nmapfile_)
        : nmapfile(std::move(nmapfile_)) {}

    GLFWwindow *window;

    eye::State eye = eye::toward_negz();

    GLuint prog;
    GLuint mvp_ubo;

    GLuint nmap_tex;

    GLuint normal_tex;
    GLuint mvp_uniform_block;

    Quad quad{{-1.0f, -1.0f}, {1.0f, 1.0f}};

    bool esc_pressed = false;

    fs::path nmapfile;
};

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, k_screen_width, k_screen_height, 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    // Create program
    app.prog = eng::create_program(vs_quad, fs_quad);

    // Create quad
    app.quad.make_vao();

    // Create texture
    glActiveTexture(GL_TEXTURE0);
    app.nmap_tex = dds::load_texture(app.nmapfile, 0, nullptr, &memory_globals::default_allocator());

    // Initialize MVP uniform
    // app.mvp.proj_mat = persp_proj(0.1, 1000.0, 70.0 * one_deg_in_rad, k_screen_width / k_screen_height);
    // eye::update_view_transform(app.eye, app.mvp.view_mat);

    // Create and initialize mvp ubo
    glUseProgram(app.prog);

    glClearColor(0.8f, 0.9f, 0.9f, 1.0f);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

template <> void update<App>(App &app, State &s) {
    glfwPollEvents();

    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }

#if 0
    if (eng::handle_eye_input(app.window, app.eye, s.frame_time_in_sec, app.mvp.view_mat)) {
        glBindBuffer(GL_UNIFORM_BUFFER, app.mvp_ubo);
        // glBufferData(GL_UNIFORM_BUFFER, sizeof(MVP), &app.mvp, GL_DYNAMIC_DRAW);
    }
#endif
}

template <> void render<App>(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(app.quad.vao);
    glDrawArrays(GL_TRIANGLES, 0, ARRAY_SIZE(app.quad.vertices));

    glfwSwapBuffers(app.window);
}

template <> bool should_close<App>(App &app) { return app.esc_pressed || glfwWindowShouldClose(app.window); }

template <> void close<App>(App &app) { eng::close_gl(); }

} // namespace app_loop

int main(int ac, char **av) {
    memory_globals::init(1024);

    // clang-format off
    cxxopts::Options desc("Allowed options");
    desc.add_options()("help", "produce help message")
        ("normalmap", po::value<std::string>(), "path to normal map file");
    // clang-format on

    auto result = desc.parse(ac, av);

    CHECK_F(result.count("normalmap") == 1, "Argument `normalmap` is required");
    {
        App app(result["normalmap"].as<std::string>());
        app_loop::State aresultpp_state{};

        app_loop::run(app, app_state);
    }

    memory_globals::shutdown();
}
