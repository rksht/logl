#include "distance_field.h"
#include "essentials.h"

using namespace fo;
using namespace math;

GLFWwindow *window;
constexpr int WINDOW_WIDTH = 1024;
constexpr int WINDOW_HEIGHT = 768;

constexpr float DF_TEXTURE_WIDTH = 400;
constexpr float DF_TEXTURE_HEIGHT = 400;

constexpr float BOUNDARY_CIRCLE_RADIUS = 40;
static_assert(DF_TEXTURE_WIDTH == DF_TEXTURE_HEIGHT, "For simplicity");

// Generate circle vertices on the 2D plane
void gen_2dcircle_vertices(uint32_t count, const Vector2 &center, float radius, std::vector<Vector2> &dest) {
    float angle_per_arc = float(2.0 * pi / count);

    const size_t vfirst = dest.size();
    dest.resize(vfirst + count);

    for (uint32_t i = 0; i < count; ++i) {
        float a = i * angle_per_arc;
        dest[vfirst + i] = Vector2{center.x + radius * std::cos(a), center.y + radius * std::sin(a)};
    }
}

void ok_go() {
    eng::init_default_string_table(30, 10);

    eng::start_gl(&window, WINDOW_WIDTH, WINDOW_HEIGHT);
    eng::enable_debug_output(nullptr, nullptr);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);

    fs::path DF_FILE;

    eng::FileMonitor fm;

    {
        auto ini = inistorage::Storage(fs::path(SOURCE_DIR) / "glyph-files.txt");
        auto file = ini.string("circle_df_path");
        assert(file != inistorage::DefaultValue<std::string>::value());
        DF_FILE = simple_absolute_path(file.c_str());
    }

    auto df = DistanceField::from_df_file(DF_FILE);

    CHECK_F(df.w == DF_TEXTURE_WIDTH && df.h == DF_TEXTURE_HEIGHT, "Mismatch in .cpp and .py files");

    GLuint df_texture;
    glGenTextures(1, &df_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, df_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, df.w, df.h, 0, GL_RED, GL_FLOAT, df.v.data());
    df.free_vector();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    constexpr float QUAD_WIDTH_IN_WORLD = 1.0f;
    constexpr float QUAD_HALFWIDTH_IN_WORLD = QUAD_WIDTH_IN_WORLD / 2.0f;

    Quad quad({-QUAD_HALFWIDTH_IN_WORLD, -QUAD_HALFWIDTH_IN_WORLD},
              {QUAD_HALFWIDTH_IN_WORLD, QUAD_HALFWIDTH_IN_WORLD}, -3.0f);
    GLuint quad_vao, quad_vbo;
    quad.make_vao(&quad_vbo, &quad_vao);

    struct WVP_UniformFormat {
        Matrix4x4 view;
        Matrix4x4 proj;
    };

    WVP_UniformFormat wvp;
    eye::State eye = eye::toward_negz(2.0f);
    eye::update_view_transform(eye, wvp.view);
    wvp.proj = persp_proj(0.1f, 1000.0f, 70.0f * one_deg_in_rad, float(WINDOW_WIDTH) / WINDOW_HEIGHT);

    GLuint wvp_ubo = create_uniform_buffer(0, sizeof(WVP_UniformFormat));
    glBindBuffer(GL_UNIFORM_BUFFER, wvp_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WVP_UniformFormat), &wvp);

    auto vs_texture = eng::create_monitored_shader(
        fm, eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n", GL_VERTEX_SHADER,
        CSTR("#define QUAD_VS\n"), fs::path(SOURCE_DIR) / "vis_df.vert");

    auto fs_texture = eng::create_monitored_shader(
        fm, eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n", GL_FRAGMENT_SHADER,
        CSTR("#define QUAD_FS\n"), fs::path(SOURCE_DIR) / "vis_df.frag");

    eng::ShaderProgramInfo quad_program;
    quad_program.attach_shader(vs_texture);
    quad_program.attach_shader(fs_texture);
    quad_program.create_handle();

    auto vs_circle = eng::create_monitored_shader(
        fm, eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n", GL_VERTEX_SHADER,
        CSTR("#define CIRCLE_VS\n"), fs::path(SOURCE_DIR) / "vis_df.vert");

    auto fs_circle = eng::create_monitored_shader(
        fm, eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n", GL_FRAGMENT_SHADER,
        CSTR("#define CIRCLE_FS\n"), fs::path(SOURCE_DIR) / "vis_df.frag");

    eng::ShaderProgramInfo circles_program;
    circles_program.attach_shader(vs_circle);
    circles_program.attach_shader(fs_circle);
    circles_program.create_handle();

    GLuint uloc_line_color = glGetUniformLocation((GLuint)circles_program, "line_color");

    // I will make some concentric circles, and use GL_LINE_STRIP primitive to draw them. The circles will
    // be transformed the same way as the textured quad and will be draw just above it. The boundary
    // circle's radius in texture space is first normalized.

    // constexpr float NORMALIZED_CIRCLE_RADIUS = BOUNDARY_CIRCLE_RADIUS / DF_TEXTURE_WIDTH;

    // Radial distance between adjacent circles in texture space
    constexpr float RADIAL_DISTANCE = 5;

    // constexpr float NORMALIZED_RADIAL_DISTANCE = RADIAL_DISTANCE / DF_TEXTURE_WIDTH;

    // Will be used to source the vbo for the concentric circles
    std::vector<Vector2> circle_vertices;

    constexpr uint32_t VERTICES_PER_CIRCLE = 100;

    uint32_t circle_count = 0;

    // Generate the vertices
    {
        float prev_radius = std::numeric_limits<float>::max();
        uint32_t p = ~uint32_t(0);
        for (float radius = RADIAL_DISTANCE; radius < BOUNDARY_CIRCLE_RADIUS * 2; radius += RADIAL_DISTANCE) {
            float normalized_radius = radius / DF_TEXTURE_WIDTH;
            gen_2dcircle_vertices(VERTICES_PER_CIRCLE, Vector2{0.0f, 0.0f}, normalized_radius,
                                  circle_vertices);

            ++circle_count;

            if (p == ~uint32_t(0) &&
                ((prev_radius < BOUNDARY_CIRCLE_RADIUS && radius >= BOUNDARY_CIRCLE_RADIUS) ||
                 (prev_radius <= BOUNDARY_CIRCLE_RADIUS && radius > BOUNDARY_CIRCLE_RADIUS))) {
                float d1 = std::abs(prev_radius - BOUNDARY_CIRCLE_RADIUS);
                float d2 = std::abs(radius - BOUNDARY_CIRCLE_RADIUS);
                if (d1 < d2) {
                    p = (circle_count - 2) * VERTICES_PER_CIRCLE;
                } else {
                    p = (circle_count - 1) * VERTICES_PER_CIRCLE;
                }
            }
            prev_radius = radius;
        }

        fo::Array<Vector2> tmp(memory_globals::default_allocator(), VERTICES_PER_CIRCLE);
        std::copy(circle_vertices.begin() + p, circle_vertices.begin() + (p + VERTICES_PER_CIRCLE),
                  begin(tmp));
        std::copy(circle_vertices.begin(), circle_vertices.begin() + VERTICES_PER_CIRCLE,
                  circle_vertices.begin() + p);
        std::copy(begin(tmp), end(tmp), circle_vertices.begin());

        LOG_F(INFO, "Circle count = %u, Vertices buffer size = %zu KB", circle_count,
              vec_bytes(circle_vertices) / 1024);
    }

    // The vao and vbo for the circles
    GLuint circles_vao, circles_vbo;
    glGenBuffers(1, &circles_vbo);
    glGenVertexArrays(1, &circles_vao);
    glBindVertexArray(circles_vao);
    glBindBuffer(GL_ARRAY_BUFFER, circles_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(circle_vertices), circle_vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribBinding(0, 0);
    glVertexAttribFormat(0, 2, GL_FLOAT, GL_FALSE, GLuint(0));

    stop_watch::State<std::chrono::high_resolution_clock> sw{};
    stop_watch::start(sw);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        fm.poll_changes();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        const float dt = std::chrono::duration<float, std::ratio<1, 1>>(stop_watch::restart(sw)).count();

        if (eng::handle_eye_input(window, eye, dt, wvp.view)) {
            glBindBuffer(GL_UNIFORM_BUFFER, wvp_ubo);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WVP_UniformFormat), &wvp);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram((GLuint)quad_program);
        glBindVertexArray(quad_vao);
        glBindVertexBuffer(0, quad_vbo, 0, sizeof(Quad::VertexData));
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Draw the circles

        glUseProgram((GLuint)circles_program);
        glBindVertexArray(circles_vao);

        // Exact circle
        glUniform4f(uloc_line_color, 1.0f, 1.0f, 0.0f, 1.0f);
        glBindVertexBuffer(0, circles_vbo, 0, sizeof(Vector2));
        glDrawArrays(GL_LINE_LOOP, 0, VERTICES_PER_CIRCLE);
        // Other circles
        glUniform4f(uloc_line_color, 1.0f, 0.0f, 0.0f, 1.0f);
        for (uint32_t i = 1; i < circle_count; ++i) {
            glBindVertexBuffer(0, circles_vbo, i * VERTICES_PER_CIRCLE * sizeof(Vector2), sizeof(Vector2));
            glDrawArrays(GL_LINE_LOOP, 0, VERTICES_PER_CIRCLE);
        }

        glfwSwapBuffers(window);
    }
}

int main() {
    memory_globals::init();
    DEFER( []() { memory_globals::shutdown(); });
    { ok_go(); }
}
