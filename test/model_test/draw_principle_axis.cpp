// A visualization for the principal axis calculation routine

#include "essentials.h"
#include <learnogl/imgui_glfw.h>

#define PAR_SHAPES_IMPLEMENTATION
#include <learnogl/par_shapes.h>

#include <learnogl/bounding_shapes.h>
#include <learnogl/mesh.h>

#undef max

#ifndef SOURCE_DIR
#error "SOURCE_DIR not defined"
#endif

using namespace math;
using namespace fo;
using namespace mesh;

#define ALLOCATOR memory_globals::default_allocator()

constexpr float MAX_FLOAT = std::numeric_limits<float>::max();

const int WIDTH = 1024;
const int HEIGHT = 768;
constexpr float NEAR_Z = 0.1f;
constexpr float FAR_Z = 100.0f;

static u32 g_current_buffer = 0;

struct GenerateInfo {
    Vector3 cube_radius = {1.0, 1.0, 1.0};
    Vector3 preferences = {20, 20, 20};
    Vector3 center = {0, 0, 0};
};

constexpr int SHAPE_TYPE_RECT = 0;
constexpr int SHAPE_TYPE_SPHERE = 1;

struct App {
    GLFWwindow *window;
    eye::State eye;

    GLuint points_vao;
    GLuint points_vbo;
    GLuint lines_vbo;
    GLuint lines_vao;

    GLuint cube_vao;
    GLuint sphere_vao;
    uint32_t num_cube_indices;
    uint32_t num_sphere_indices;

    struct {
        Matrix4x4 mat_view;
        Matrix4x4 mat_proj;
    } cam_mats;

    Array<Vector3> points{ALLOCATOR};
    alignas(16) Vector3 axes_lines[6 + 6 + 6 + 6]; // Axes lines, Extent lines, Color

    Matrix4x4 bounding_ob_to_world;

    // Shader info
    struct {
        GLuint name;
        GLuint uloc_eye_pos;
    } points_shader;

    struct {
        GLuint name;
    } lines_shader;

    struct {
        GLuint name;
        GLuint uloc_ob_to_world;
    } cube_shader;

    GLuint points_texture;

    GLuint cam_mats_ubo;

    GenerateInfo generate_info;
    int current_shape = SHAPE_TYPE_RECT;
    bool esc_pressed = false;
};

void init_shader_info(App &app);

const int NUM_POINTS = 100;

void generate_point_cloud(Array<Vector3> &points, const GenerateInfo &gi, const uint32_t num_points) {
    assert(num_points == NUM_POINTS);

    resize(points, num_points);

    float sum = gi.preferences.x + gi.preferences.y + gi.preferences.z;

    Vector3 probability = gi.preferences / sum;

    float x_chunk_max = 100.0f * probability.x;
    float y_chunk_max = x_chunk_max + 100.0f * probability.y;

    for (uint32_t i = 0; i < num_points; ++i) {
        Vector3 &point = points[i];

        float dice = rng::random(0.0f, 100.0f);
        if (0.0 <= dice && dice < x_chunk_max) {
            point.x = rng::random(-gi.cube_radius.x, gi.cube_radius.x);
            point.y = rng::random(-gi.cube_radius.y / 2, gi.cube_radius.y / 2);
            point.z = rng::random(-gi.cube_radius.z / 2, gi.cube_radius.z / 2);
        } else if (x_chunk_max <= dice && dice < y_chunk_max) {
            point.y = rng::random(-gi.cube_radius.y, gi.cube_radius.y);
            point.x = rng::random(-gi.cube_radius.x / 2, gi.cube_radius.x / 2);
            point.z = rng::random(-gi.cube_radius.z / 2, gi.cube_radius.z / 2);
        } else {
            point.z = rng::random(-gi.cube_radius.z, gi.cube_radius.z);
            point.y = rng::random(-gi.cube_radius.y / 2, gi.cube_radius.y / 2);
            point.x = rng::random(-gi.cube_radius.x / 2, gi.cube_radius.x / 2);
        }
        point = point + gi.center;
    }
}

void rotate_points(Array<Vector3> &points, Vector3 euler_xyz) {
    Matrix4x4 rotx = rotation_matrix(unit_x, euler_xyz.x);
    Matrix4x4 roty = rotation_matrix(unit_y, euler_xyz.y);
    Matrix4x4 rotz = rotation_matrix(unit_z, euler_xyz.z);

    Matrix4x4 mat = rotz * (roty * rotx);

    for (Vector3 &point : points) {
        point = Vector3(mat * Vector4(point, 1.0f));
    }
}

void calculate_axes_lines(Array<Vector3> &points,
                          Vector3 *lines_buffer,
                          const PrincipalAxis &pa,
                          const BoundingRect &bb) {
    lines_buffer[0] = bb.center + bb.half_extent.x * pa.axes[0];
    lines_buffer[1] = bb.center - bb.half_extent.x * pa.axes[0];
    lines_buffer[2] = bb.center + bb.half_extent.y * pa.axes[1];
    lines_buffer[3] = bb.center - bb.half_extent.y * pa.axes[1];
    lines_buffer[4] = bb.center + bb.half_extent.z * pa.axes[2];
    lines_buffer[5] = bb.center - bb.half_extent.z * pa.axes[2];

    float minmaxdist[6] = {};

    Array<float> dot_prods{ALLOCATOR, size(points)};
    int p = 6;
    for (uint32_t i = PrincipalAxis::R; i <= PrincipalAxis::T; ++i) {
        const Vector3 &axis = pa.axes[i];

        for (uint32_t j = 0; j < size(points); ++j) {
            dot_prods[j] = dot(points[j], axis);
        }

        uint32_t max = 0;
        uint32_t min = 0;
        for (uint32_t j = 1; j < size(dot_prods); ++j) {
            if (dot_prods[j] < dot_prods[min]) {
                min = j;
                minmaxdist[i * 2] = dot_prods[j];
            }
            if (dot_prods[j] > dot_prods[max]) {
                max = j;
                minmaxdist[i * 2 + 1] = dot_prods[j];
            }
        }

        lines_buffer[p++] = points[min];
        lines_buffer[p++] = points[max];
    }
    log_info(R"(
        Min and max along R axis: %f, %f | [%f, %f, %f], [%f, %f, %f]
        Min and max along S axis: %f, %f | [%f, %f, %f], [%f, %f, %f]
        Min and max along T axis: %f, %f | [%f, %f, %f], [%f, %f, %f]
    )",
             minmaxdist[0],
             minmaxdist[1],
             XYZ(lines_buffer[6]),
             XYZ(lines_buffer[6 + 1]),
             minmaxdist[2],
             minmaxdist[3],
             XYZ(lines_buffer[6 + 2]),
             XYZ(lines_buffer[6 + 3]),
             minmaxdist[4],
             minmaxdist[5],
             XYZ(lines_buffer[6 + 4]),
             XYZ(lines_buffer[6 + 5]));
}

void calculate_axes_lines(Array<Vector3> &points,
                          Vector3 *lines_buffer,
                          const PrincipalAxis &pa,
                          const BoundingSphere &bs) {
    lines_buffer[0] = bs.center + bs.radius * pa.axes[0];
    lines_buffer[1] = bs.center - bs.radius * pa.axes[0];
    lines_buffer[2] = bs.center + bs.radius * pa.axes[1];
    lines_buffer[3] = bs.center - bs.radius * pa.axes[1];
    lines_buffer[4] = bs.center + bs.radius * pa.axes[2];
    lines_buffer[5] = bs.center - bs.radius * pa.axes[2];

    float minmaxdist[6] = {};

    Array<float> dot_prods{ALLOCATOR, size(points)};
    int p = 6;
    for (uint32_t i = PrincipalAxis::R; i <= PrincipalAxis::T; ++i) {
        const Vector3 &axis = pa.axes[i];

        for (uint32_t j = 0; j < size(points); ++j) {
            dot_prods[j] = dot(points[j], axis);
        }

        uint32_t max = 0;
        uint32_t min = 0;
        for (uint32_t j = 1; j < size(dot_prods); ++j) {
            if (dot_prods[j] < dot_prods[min]) {
                min = j;
                minmaxdist[i * 2] = dot_prods[j];
            }
            if (dot_prods[j] > dot_prods[max]) {
                max = j;
                minmaxdist[i * 2 + 1] = dot_prods[j];
            }
        }

        lines_buffer[p++] = points[min];
        lines_buffer[p++] = points[max];
    }
    log_info(R"(
        Min and max along R axis: %f, %f | [%f, %f, %f], [%f, %f, %f]
        Min and max along S axis: %f, %f | [%f, %f, %f], [%f, %f, %f]
        Min and max along T axis: %f, %f | [%f, %f, %f], [%f, %f, %f]
    )",
             minmaxdist[0],
             minmaxdist[1],
             XYZ(lines_buffer[6]),
             XYZ(lines_buffer[6 + 1]),
             minmaxdist[2],
             minmaxdist[3],
             XYZ(lines_buffer[6 + 2]),
             XYZ(lines_buffer[6 + 3]),
             minmaxdist[4],
             minmaxdist[5],
             XYZ(lines_buffer[6 + 4]),
             XYZ(lines_buffer[6 + 5]));
}

void recompute_bounding_rect(App &app, const PrincipalAxis &pa, const BoundingRect &bb) {
    // Rotation to the principle axis and then scale by the extents in each direction
    Matrix4x4 t = translation_matrix(-0.5f, -0.5f, -0.5f); // Par shapes need to be shifted to origin
    Matrix4x4 scale = xyz_scale_matrix(2 * bb.half_extent);
    Matrix4x4 orient = Matrix4x4{
        Vector4(pa.axes[0], 0), Vector4(pa.axes[1], 0), Vector4(pa.axes[2], 0), Vector4{0, 0, 0, 1}};
    Matrix4x4 recenter = translation_matrix(bb.center);
    app.bounding_ob_to_world = recenter * (orient * (scale * t));
}

void recompute_bounding_sphere(App &app, const PrincipalAxis &pa, const BoundingSphere &bs) {
    // Matrix4x4 t = translation_matrix(-0.5f, -0.5f, -0.5f);
    Matrix4x4 t = identity_matrix;
    Matrix4x4 scale = xyz_scale_matrix(bs.radius, bs.radius, bs.radius);
    Matrix4x4 orient = Matrix4x4{
        Vector4(pa.axes[0], 0), Vector4(pa.axes[1], 0), Vector4(pa.axes[2], 0), Vector4{0, 0, 0, 1}};
    Matrix4x4 recenter = translation_matrix(bs.center);
    app.bounding_ob_to_world = recenter * (orient * (scale * t));
}

namespace app_loop {

eng::StartGLParams gl_params;

template <> void init<App>(App &app) {
    gl_params.window_width = WIDTH;
    gl_params.window_height = HEIGHT;
    gl_params.wait_refresh_before_swap = false;
    gl_params.double_buffer_enabled = true;
    // eng::start_gl(&app.window, WIDTH, HEIGHT, "draw_principal_axis", 4, 4);
    app.window = eng::start_gl(gl_params);
    eng::enable_debug_output(nullptr, nullptr);

    generate_point_cloud(app.points, app.generate_info, NUM_POINTS);
    rotate_points(app.points, Vector3{0.0, 0.0, 0.0});

    glGenBuffers(1, &app.points_vbo);

    glGenVertexArrays(1, &app.points_vao);
    glBindVertexArray(app.points_vao);

    glBindBuffer(GL_ARRAY_BUFFER, app.points_vbo);

    // Have points and axes lines in same buffer? why not.
    const size_t total_size = sizeof(Vector3) * size(app.points) + 6 * sizeof(Vector3);
    glBufferData(GL_ARRAY_BUFFER, total_size, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vector3) * size(app.points), data(app.points));

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3), (GLvoid *)0);
    glEnableVertexAttribArray(0);

    // Will calculate the very first axes lines now
    PrincipalAxis pa = calculate_principal_axis(data(app.points), size(app.points));

    BoundingSphere bs = create_bounding_sphere(pa, data(app.points), size(app.points));
    recompute_bounding_sphere(app, pa, bs);

    BoundingRect bb = create_bounding_rect(pa, data(app.points), size(app.points));
    calculate_axes_lines(app.points, app.axes_lines, pa, bb);
    recompute_bounding_rect(app, pa, bb);

    // Lines color, set one time
    app.axes_lines[6 + 6] = unit_x;
    app.axes_lines[6 + 6 + 1] = unit_x;
    app.axes_lines[6 + 6 + 2] = unit_y;
    app.axes_lines[6 + 6 + 3] = unit_y;
    app.axes_lines[6 + 6 + 4] = unit_z;
    app.axes_lines[6 + 6 + 5] = unit_z;
    app.axes_lines[6 + 6 + 6] = unit_x;
    app.axes_lines[6 + 6 + 7] = unit_x;
    app.axes_lines[6 + 6 + 8] = unit_y;
    app.axes_lines[6 + 6 + 9] = unit_y;
    app.axes_lines[6 + 6 + 10] = unit_z;
    app.axes_lines[6 + 6 + 11] = unit_z;

    // Set the lines array format
    static_assert(sizeof(Vector3) == 3 * sizeof(float), "");

    log_info("num points = %u", size(app.points));

    glGenVertexArrays(1, &app.lines_vao);
    glBindVertexArray(app.lines_vao);

    glGenBuffers(1, &app.lines_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, app.lines_vbo);

    glBufferData(GL_ARRAY_BUFFER, (6 + 6 + 6 + 6) * sizeof(Vector3), app.axes_lines, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3), (GLvoid *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vector3), (GLvoid *)(sizeof(Vector3) * (6 + 6)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    {
        glGenVertexArrays(1, &app.cube_vao);
        glBindVertexArray(app.cube_vao);

        // Create the cube vao
        par_shapes_mesh *cube = par_shapes_create_cube();
        GLuint cube_vbo, cube_ebo;
        glGenBuffers(1, &cube_vbo);
        glGenBuffers(1, &cube_ebo);
        glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * cube->npoints, cube->points, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     sizeof(PAR_SHAPES_T) * cube->ntriangles * 3,
                     cube->triangles,
                     GL_STATIC_DRAW);
        app.num_cube_indices = cube->ntriangles * 3;

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (GLvoid *)0);
        glEnableVertexAttribArray(0);
    }

    // Create the sphere vao
    {

        glGenVertexArrays(1, &app.sphere_vao);
        glBindVertexArray(app.sphere_vao);

        par_shapes_mesh *sphere = par_shapes_create_parametric_sphere(20, 20);
        GLuint sphere_vbo, sphere_ebo;
        glGenBuffers(1, &sphere_vbo);
        glGenBuffers(1, &sphere_ebo);
        glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * sphere->npoints, sphere->points, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphere_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     sizeof(uint16_t) * sphere->ntriangles * 3,
                     sphere->triangles,
                     GL_STATIC_DRAW);
        app.num_sphere_indices = sphere->ntriangles * 3;

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (GLvoid *)0);
        glEnableVertexAttribArray(0);
    }

    // Usual
    app.eye = eye::toward_negz(2.0f);
    eye::update_view_transform(app.eye, app.cam_mats.mat_view);
    app.cam_mats.mat_proj = persp_proj(NEAR_Z, FAR_Z, one_deg_in_rad * 70.0, float(WIDTH) / HEIGHT);
    // app.cam_mats.mat_proj = orthographic_projection(0.1, 100.0, 0.0, 0.0, WIDTH, HEIGHT);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &app.cam_mats_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.cam_mats_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(app.cam_mats), &app.cam_mats, GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, app.cam_mats_ubo);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT);

    app.points_texture = load_texture(
        SOURCE_DIR "/particle.png", TextureFormat{GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE}, GL_TEXTURE0);

    init_shader_info(app);

    glUseProgram(app.points_shader.name);
    glUniform1i(glGetUniformLocation(app.points_shader.name, "points_texture"), 0);
    glUniform3f(app.points_shader.uloc_eye_pos, app.eye.position.x, app.eye.position.y, app.eye.position.z);

    glUseProgram(app.cube_shader.name);
    glUniformMatrix4fv(app.cube_shader.uloc_ob_to_world, 1, GL_FALSE, (float *)&app.bounding_ob_to_world);

    // Init imgui
    imgui::init(app.window, imgui::InstallCallbacks{false, true, true, true, true});

    auto io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(LOGL_UI_FONT, 15.0f);

    // glLineWidth(3.0f);

    glClearColor(0.95f, 0.95f, 0.95f, 1.0f);

    glEnable(GL_LINE_SMOOTH);

    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
}

#define XYZ(v) (v).x, (v).y, (v).z

template <> void update<App>(App &app, State &app_state) {
    glfwPollEvents();

    imgui::update();

    // Eye input
    if (eng::handle_eye_input(app.window, app.eye, app_state.frame_time_in_sec, app.cam_mats.mat_view)) {
        glBindBuffer(GL_UNIFORM_BUFFER, app.cam_mats_ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(app.cam_mats), &app.cam_mats, GL_DYNAMIC_DRAW);

        glUseProgram(app.points_shader.name);
        glUniform3f(
            app.points_shader.uloc_eye_pos, app.eye.position.x, app.eye.position.y, app.eye.position.z);
    }

    bool recalculate_axes = false;
    bool shape_change = false;

    {
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);

        ImGui::Text("R: [%.2f %.2f %.2f], S: [%.2f %.2f %.2f], T: [%.2f %.2f %.2f]",
                    XYZ(app.bounding_ob_to_world.x),
                    XYZ(app.bounding_ob_to_world.y),
                    XYZ(app.bounding_ob_to_world.z));

        Vector3 cube_radius = app.generate_info.cube_radius;
        ImGui::DragFloat3("3D Cube", (float *)&cube_radius, 1.0f, 0.0f, 40.0f);

        Vector3 preferences = app.generate_info.preferences;
        ImGui::DragFloat3("Axis Preferences", (float *)&preferences, 1.0f, 0.0f, 40.0f);

        Vector3 center = app.generate_info.center;
        ImGui::DragFloat3("Center", (float *)&center, 0.1f, -5.0f, 5.0f);

        if (cube_radius != app.generate_info.cube_radius || preferences != app.generate_info.preferences ||
            center != app.generate_info.center) {
            app.generate_info.cube_radius = cube_radius;
            app.generate_info.preferences = preferences;
            app.generate_info.center = center;
            recalculate_axes = true;
        }

        int shape_radio_button = app.current_shape;

        ImGui::RadioButton("Rect", &shape_radio_button, SHAPE_TYPE_RECT);
        ImGui::RadioButton("Sphere", &shape_radio_button, SHAPE_TYPE_SPHERE);

        if (shape_radio_button != app.current_shape) {
            app.current_shape = shape_radio_button;
            shape_change = true;
        }
    }

    const auto regen_bounding_rect = [&]() {
        PrincipalAxis pa = calculate_principal_axis(data(app.points), size(app.points));
        BoundingRect bb = create_bounding_rect(pa, data(app.points), size(app.points));
        calculate_axes_lines(app.points, app.axes_lines, pa, bb);
        recompute_bounding_rect(app, pa, bb);
        glUseProgram(app.cube_shader.name);
        glUniformMatrix4fv(app.cube_shader.uloc_ob_to_world, 1, GL_FALSE, (float *)&app.bounding_ob_to_world);

        glBindBuffer(GL_ARRAY_BUFFER, app.points_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(Vector3) * size(app.points),
                     data(app.points),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, app.lines_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vector3) * (6 + 6), app.axes_lines);
    };

    const auto regen_bounding_sphere = [&]() {
        PrincipalAxis pa = calculate_principal_axis(data(app.points), size(app.points));
        BoundingSphere bs = create_bounding_sphere(pa, data(app.points), size(app.points));
        LOG_F(INFO, "Sphere center = [%.3f, %.3f,%.3f], radius = %.3f", XYZ(bs.center), bs.radius);
        calculate_axes_lines(app.points, app.axes_lines, pa, bs);
        recompute_bounding_sphere(app, pa, bs);
        glUseProgram(app.cube_shader.name);
        glUniformMatrix4fv(app.cube_shader.uloc_ob_to_world, 1, GL_FALSE, (float *)&app.bounding_ob_to_world);

        glBindBuffer(GL_ARRAY_BUFFER, app.points_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(Vector3) * size(app.points),
                     data(app.points),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, app.lines_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vector3) * (6 + 6), app.axes_lines);
    };

    if (recalculate_axes) {
        LOG_F(INFO, "DRAWING BOUNDING %s", app.current_shape == SHAPE_TYPE_RECT ? "RECT" : "SPHERE");
        generate_point_cloud(app.points, app.generate_info, NUM_POINTS);

        if (app.current_shape == SHAPE_TYPE_RECT) {
            regen_bounding_rect();
        } else {
            regen_bounding_sphere();
        }
    }

    if (shape_change) {
        LOG_F(INFO,
              "Changing to %s shape (%i)",
              app.current_shape == SHAPE_TYPE_RECT ? "RECT" : "SPHERE",
              app.current_shape);
        if (app.current_shape == SHAPE_TYPE_RECT) {
            regen_bounding_rect();
        } else {
            regen_bounding_sphere();
        }
    }

    // Esc
    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindVertexArray(app.lines_vao);
    glUseProgram(app.lines_shader.name);
    glDrawArrays(GL_LINES, 0, 12);

    glUseProgram(app.cube_shader.name);

    if (app.current_shape == SHAPE_TYPE_RECT) {
        glBindVertexArray(app.cube_vao);
        glDrawElements(GL_TRIANGLES, app.num_cube_indices, GL_UNSIGNED_SHORT, (void *)0);
    } else {
        glBindVertexArray(app.sphere_vao);
        glDrawElements(GL_TRIANGLES, app.num_sphere_indices, GL_UNSIGNED_SHORT, (void *)0);
    }

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(app.points_vao);
    glUseProgram(app.points_shader.name);
    glDrawArrays(GL_POINTS, 0, size(app.points));

    glEnable(GL_DEPTH_TEST);

    imgui::render();

    glfwSwapBuffers(app.window);
}

template <> bool should_close<App>(App &app) { return glfwWindowShouldClose(app.window) || app.esc_pressed; }

template <> void close<App>(App &app) {
    imgui::shutdown();
    eng::close_gl(gl_params);
}

} // namespace app_loop

void init_shader_info(App &app) {
    app.points_shader.name =
        create_vs_fs_prog(SOURCE_DIR "/points_draw.vert", SOURCE_DIR "/points_draw.frag");
    app.lines_shader.name = create_vs_fs_prog(SOURCE_DIR "/lines_draw.vert", SOURCE_DIR "/lines_draw.frag");

    app.cube_shader.name = create_vs_fs_prog(SOURCE_DIR "/cube_draw.vert", SOURCE_DIR "/cube_draw.frag");

    app.points_shader.uloc_eye_pos = glGetUniformLocation(app.points_shader.name, "eye_pos");
    app.cube_shader.uloc_ob_to_world = glGetUniformLocation(app.cube_shader.name, "bounding_ob_to_world");
}

int main() {
    memory_globals::init();
    rng::init_rng(0xdeadbeef);
    {
        App app;
        app_loop::State app_state{};
        app_loop::run(app, app_state);
    }
    memory_globals::shutdown();
}
