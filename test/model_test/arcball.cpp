#include <glad/glad.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <assert.h>
#include <learnogl/app_loop.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/eye.h>
#include <learnogl/eng>
#include <learnogl/math_ops.h>
#include <learnogl/mesh.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/rng.h>
#include <learnogl/stb_image.h>
#include <scaffold/debug.h>
#include <scaffold/memory.h>
#include <stddef.h> // offsetof
#include <utility>

#define PAR_SHAPES_IMPLEMENTATION
#include <learnogl/par_shapes.h>

#include "cwd.h"

using namespace math;
using namespace fo;
using namespace mesh;

#define ALLOCATOR memory_globals::default_allocator()

constexpr float MAX_FLOAT = std::numeric_limits<float>::max();

constexpr int width = 1024;
constexpr int height = 768;
constexpr float NEAR = 0.1f;
constexpr float FAR = 100.0f;

constexpr size_t MAX_MESHES_IN_MODEL = 10;

struct ClickAndHoldState {
    // Last noted point on sphere
    Vector3 start_point;

    struct {
        Vector3 position;       // Position of the center of the arc ball
        Quaternion orientation; // Orientation of the arc ball (in model frame as we use it)
    } arc_ball;

    bool mouse_currently_pressed;

    // Constructor will create a 'not started yet' state
    ClickAndHoldState(const Vector3 &position = Vector3{0.f, 0.f, 0.f})
        : start_point(MAX_FLOAT, 0.f, 0.f)
        , arc_ball{position, identity_versor}
        , mouse_currently_pressed(false) {}
};

static bool ch_is_reset(const ClickAndHoldState &ch) { return ch.start_point.x == MAX_FLOAT; }

static void ch_start(ClickAndHoldState &ch, const Vector3 &start_point) {
    ch.start_point = start_point;
    ch.mouse_currently_pressed = true;
}

// Call when you want to calculate the orientation
static void ch_restart(ClickAndHoldState &ch, const Vector3 &stop_point) {
    assert(ch.start_point.x != MAX_FLOAT);

    const Vector3 v1 = normalize(ch.start_point - ch.arc_ball.position);
    const Vector3 v2 = normalize(stop_point - ch.arc_ball.position);
    const Vector3 axis = normalize(cross(v1, v2));
    // Calulate versor representing the rotation
    auto rotation = versor_from_axis_angle(axis, dot(v1, v2) * 0.05);
    // Update orientation in model space
    ch.arc_ball.orientation = rotation * ch.arc_ball.orientation;
    ch.start_point = stop_point;

#if 0

    const auto orientation = axis_angle_from_versor(ch.arc_ball.orientation);

    log_info(R"(
        Orientation: {Axis = [%f, %f, %f], Angle = %f}
    )",
             XYZ(orientation.first), orientation.second / one_deg_in_rad);

    if (orientation.second != orientation.second) {
        debug("GOTTA DEBUG");
    }

#endif
}

static void ch_stop(ClickAndHoldState &ch) {
    ch.start_point.x = MAX_FLOAT;
    ch.start_point.y = ch.start_point.z = 0.f;
    ch.mouse_currently_pressed = false;
}

static Matrix4x4 ch_make_matrix(const ClickAndHoldState &ch) {
    return matrix_from_versor(ch.arc_ball.orientation);
}

struct App {
    BoundingSphere sphere;

    eye::State eye;
    Matrix4x4 view_mat;
    Matrix4x4 proj_mat;

    ClickAndHoldState hold_state;

    Matrix4x4 inv_proj_mat;

    GLFWwindow *window;

    unsigned num_meshes;
    GLuint model_vaos[MAX_MESHES_IN_MODEL];          // vao for each mesh
    unsigned model_num_indices[MAX_MESHES_IN_MODEL]; // num vertices in each mesh
    GLuint sphere_vao;
    unsigned num_sphere_indices;

    GLuint program;
    GLuint model_loc;
    GLuint view_loc;
    GLuint proj_loc;

    struct {
        GLuint progid;
        struct {
            GLuint radius, translate, mat_view, mat_proj;
            GLuint sphere_color;
        } unilocs;
    } sphere_prog;

    bool reupload_model_mat;

    bool esc_pressed;
}; // struct App

// Returns a point in clip space that projects onto the given (wnd_x, wnd_y)
// window coordinate. The returned point when transformed to view space, will
// be on the near plane.
inline Vector4 window_to_clip_coord(const App &app, float wnd_x, float wnd_y) {
    const float ndc_x = (2 * wnd_x) / width - 1;
    const float ndc_y = -(2 * wnd_y) / height + 1;
    const Vector4 clip = {ndc_x * NEAR, ndc_y * NEAR, -NEAR, NEAR}; // Point in clip space
    return clip;
}

inline Vector4 window_to_view_coord(const App &app, float wnd_x, float wnd_y) {
    const Vector4 clip = window_to_clip_coord(app, wnd_x, wnd_y);
    return app.inv_proj_mat * clip;
}

inline Vector4 window_to_world_coord(const App &app, float wnd_x, float wnd_y) {
    const Vector4 view = window_to_view_coord(app, wnd_x, wnd_y);

    // Get the world space by using the inverse of view transformation.
    // clang-format off
    Matrix4x4 view_to_wor = {
        Vector4{eye::right(app.eye), 0},
        Vector4{eye::up(app.eye), 0},
        Vector4{-eye::forward(app.eye), 0},
        Vector4{app.eye.position, 1.0}
    };
    // clang-format on
    // Matrix4x4 view_to_wor = inverse(app.view_mat);
    return view_to_wor * view;
}

struct Ray {
    Vector3 o;
    Vector3 d;
};

// On hitting a sphere
struct SphereHitInfo {
    bool success;
    float t; // The parameter value at which the ray intersects
};

SphereHitInfo intersect_ray_sphere(const Ray &r, const BoundingSphere &s) {
    Vector3 oc = s.center - r.o;
    float t_ca = dot(oc, r.d);
    if (t_ca < 0.0) {
        return SphereHitInfo{false, {}};
    }
    // Half chord
    float t_2hc = s.radius * s.radius - square_magnitude(oc) + t_ca * t_ca;
    if (t_2hc < 0.0) {
        return SphereHitInfo{false, {}};
    }
    return SphereHitInfo{true, t_ca - std::sqrt(t_2hc)};
}

void init_sphere_prog(App &app) {
    Array<uint8_t> vert_shader_src(ALLOCATOR);
    Array<uint8_t> frag_shader_src(ALLOCATOR);

    read_file("/home/snyp/gits/learnogl/test/model_test/sphere_draw.vert", vert_shader_src);
    read_file("/home/snyp/gits/learnogl/test/model_test/sphere_draw.frag", frag_shader_src);

    app.sphere_prog.progid = eng::create_program((const char *)data(vert_shader_src),
                                                     (const char *)data(frag_shader_src));

    app.sphere_prog.unilocs.radius = glGetUniformLocation(app.sphere_prog.progid, "radius");
    app.sphere_prog.unilocs.translate = glGetUniformLocation(app.sphere_prog.progid, "translate");
    app.sphere_prog.unilocs.mat_view = glGetUniformLocation(app.sphere_prog.progid, "mat_view");
    app.sphere_prog.unilocs.mat_proj = glGetUniformLocation(app.sphere_prog.progid, "mat_proj");
    app.sphere_prog.unilocs.sphere_color = glGetUniformLocation(app.sphere_prog.progid, "sphere_color");
}

// make the sphere mesh with the help of par_shapes =)
void create_sphere_mesh(App &app, const BoundingSphere &s) {
    par_shapes_mesh *sphere_mesh = par_shapes_create_subdivided_sphere(2);

    GLuint vbo, ebo, vao;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * sphere_mesh->npoints, sphere_mesh->points,
                 GL_STATIC_DRAW);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * 3 * sphere_mesh->ntriangles,
                 sphere_mesh->triangles, GL_STATIC_DRAW);

    constexpr GLuint POS_LOC = 0;

    glVertexAttribPointer(POS_LOC, 3, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)0);
    glEnableVertexAttribArray(POS_LOC);

    app.sphere_vao = vao;
    app.num_sphere_indices = 3 * sphere_mesh->ntriangles;

    par_shapes_free_mesh(sphere_mesh);
}

static void on_mouse_button(GLFWwindow *window, int button, int action, int mods);
static void on_mouse_move(GLFWwindow *window, double xpos, double ypos);

namespace app_loop {

template <> void init<App>(App &app) {
    constexpr auto model_file = MODEL_FILE("suzanne.obj");

    Model model(memory_globals::default_allocator(), memory_globals::default_allocator());

    log_assert(load(model, model_file, true), "Failed to load model file: %s", model_file);

    eng::start_gl(&app.window, width, height);

    eng::enable_debug_output(nullptr, nullptr);

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetMouseButtonCallback(app.window, on_mouse_button);
    glfwSetCursorPosCallback(app.window, on_mouse_move);

    app.esc_pressed = false;
    app.reupload_model_mat = false;

    constexpr auto vert_shader_src = R"(
        #version 410

        layout(location = 0) in vec3 vert_pos;
        layout(location = 1) in vec3 vert_normal;
        layout(location = 2) in vec2 vert_tex2d;

        uniform mat4 model_mat, view_mat, proj_mat;

        out ColorBlock {
            flat vec3 color;
        };

        void main() {
            color = normalize(vert_normal);
            gl_Position = proj_mat * view_mat * model_mat * vec4(vert_pos, 1.0);
        }
        )";

    constexpr auto frag_shader_src = R"(
        #version 410

        in ColorBlock {
            flat vec3 color;
        };

        out vec4 frag_color;

        void main() {
            frag_color = vec4(color, 1.0);
        }
        )";

    app.eye = eye::toward_negz(2.0f);
    eye::update_view_transform(app.eye, app.view_mat);
    app.proj_mat = perspective_projection(0.1, 100.0, 67.0 * math::one_deg_in_rad, float(width) / height);
    // app.proj_mat = orthographic_projection(0.1, 100.0, 0.0, 0.0, width, height);
    app.inv_proj_mat = inverse(app.proj_mat);

    GLuint vbos[MAX_MESHES_IN_MODEL] = {0};
    glGenBuffers(size(model._mesh_array), vbos);

    // Copy each mesh's attribute buffer to a vbo
    for (unsigned i = 0; i < size(model._mesh_array); ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, vbos[i]);
        glBufferData(GL_ARRAY_BUFFER, vertex_buffer_size(model._mesh_array[i]), model._mesh_array[i].buffer,
                     GL_STATIC_DRAW);
    }

    // Set up the vao for each mesh
    memset(app.model_vaos, 0, sizeof(app.model_vaos));
    glGenVertexArrays(size(model._mesh_array), app.model_vaos);

    // Create sphere
    auto &mesh_data = model._mesh_array[0];
    app.sphere = calculate_bounding_sphere((const Vector3 *)(mesh_data.buffer + mesh_data.position_offset),
                                           mesh::num_vertices(mesh_data));
    log_info(R"(
        Bounding sphere = {
            center: [%f %f %f]
            radius: %f
        }
    )",
             XYZ(app.sphere.center), app.sphere.radius);
    create_sphere_mesh(app, app.sphere);

    for (unsigned i = 0; i < size(model._mesh_array); ++i) {
        glBindVertexArray(app.model_vaos[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbos[i]);

        MeshData &md = model._mesh_array[i];

        // Create ebo for this mesh
        GLuint ebo;
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_buffer_size(md), md.buffer + indices_offset(md),
                     GL_STATIC_DRAW);

        // Set up the attrib array pointers
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, md.packed_attr_size,
                              (const GLvoid *)md.position_offset);
        glEnableVertexAttribArray(0);

        assert(md.normal_offset != 0);
        assert(md.tex2d_offset != 0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, md.packed_attr_size,
                              (const GLvoid *)md.normal_offset);
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, md.packed_attr_size, (const GLvoid *)md.tex2d_offset);
        glEnableVertexAttribArray(2);
    }

    app.num_meshes = size(model._mesh_array);
    debug("Num meshes: %u", app.num_meshes);
    for (unsigned i = 0; i < app.num_meshes; ++i) {
        app.model_num_indices[i] = num_indices(model._mesh_array[i]);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    app.program = eng::create_program(vert_shader_src, frag_shader_src);
    app.model_loc = glGetUniformLocation(app.program, "model_mat");
    app.view_loc = glGetUniformLocation(app.program, "view_mat");
    app.proj_loc = glGetUniformLocation(app.program, "proj_mat");
    glUseProgram(app.program);

    glUniformMatrix4fv(app.model_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&identity_matrix));
    glUniformMatrix4fv(app.view_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&app.view_mat));
    glUniformMatrix4fv(app.proj_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&app.proj_mat));

    // Sphere shader
    init_sphere_prog(app);
    glUseProgram(app.sphere_prog.progid);
    glUniformMatrix4fv(app.sphere_prog.unilocs.mat_view, 1, GL_FALSE, (const float *)&app.view_mat);
    glUniformMatrix4fv(app.sphere_prog.unilocs.mat_proj, 1, GL_FALSE, (const float *)&app.proj_mat);
    glUniform1f(app.sphere_prog.unilocs.radius, app.sphere.radius);
    glUniform3f(app.sphere_prog.unilocs.translate, app.sphere.center.x, app.sphere.center.y,
                app.sphere.center.z);
    glUniform3f(app.sphere_prog.unilocs.sphere_color, 1.0f, 1.0f, 1.0f);

    glClearColor(0.26, 0.32, 0.6, 1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Should enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_CULL_FACE); // cull face
    glCullFace(GL_BACK);    // cull back face
    glFrontFace(GL_CCW);    // GL_CCW for counter clock-wise

    glViewport(0, 0, width, height);
}

template <> void update<App>(App &app, app_loop::State &app_state) {
    glfwPollEvents();

    // Eye input
    if (eng::handle_eye_input(app.window, app.eye, app_state.frame_time_in_sec, app.view_mat)) {
        glUseProgram(app.program);
        glUniformMatrix4fv(app.view_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&app.view_mat));

        glUseProgram(app.sphere_prog.progid);
        glUniformMatrix4fv(app.sphere_prog.unilocs.mat_view, 1, GL_FALSE,
                           reinterpret_cast<const float *>(&app.view_mat));
    }

    if (app.reupload_model_mat) {
        glUseProgram(app.program);
        auto model_mat = ch_make_matrix(app_loop.hold_state);
        glUniformMatrix4fv(app.model_loc, 1, GL_FALSE, (float *)&model_mat);
        app.reupload_model_mat = false;
    }

    // Esc
    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Draw each mesh of model
    glUseProgram(app.program);
    for (unsigned i = 0; i < app.num_meshes; ++i) {
        glBindVertexArray(app.model_vaos[i]);
        // glDrawArrays(GL_TRIANGLES, 0, app.model_num_vertices[i]);
        glDrawElements(GL_TRIANGLES, app.model_num_indices[i], GL_UNSIGNED_SHORT, 0);
        glBindVertexArray(0);
    }

    // Draw sphere
    glUseProgram(app.sphere_prog.progid);
    glBindVertexArray(app.sphere_vao);
    glDrawElements(GL_TRIANGLES, app.num_sphere_indices, GL_UNSIGNED_SHORT, 0);

    // Swap buffers
    glfwSwapBuffers(app.window);
}

template <> void close<App>(App &app) {
    eng::close_gl();
    glfwTerminate();
}

template <> bool should_close<App>(App &app) { return glfwWindowShouldClose(app.window) || app.esc_pressed; }

} // namespace app_loop

static void on_mouse_button(GLFWwindow *window, int button, int action, int mods) {
    App &app = *(App *)glfwGetWindowUserPointer(window);
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    Vector4 target_point = window_to_world_coord(app, xpos, ypos);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        Ray r = {app.eye.position, normalize(Vector3(target_point) - app.eye.position)};
        auto hit = intersect_ray_sphere(r, app.sphere);
        if (!hit.success) {
            return;
        }

        Vector3 point = r.o + hit.t * r.d;

        switch (action) {
        case GLFW_PRESS:
            ch_start(app_loop.hold_state, point);
            // debug("PRESS");
            break;

        case GLFW_RELEASE:
            // Seeing some weirdness, probably due to my mouse. We should have
            // the hold already started...
            ch_stop(app_loop.hold_state);
            app.reupload_model_mat = false;
            // debug("RELEASE");
            break;

        default:
            break;
        }
    }
}

static void on_mouse_move(GLFWwindow *window, double xpos, double ypos) {
    App &app = *(App *)glfwGetWindowUserPointer(window);

    if (!app_loop.hold_state.mouse_currently_pressed) {
        return;
    }

    Vector4 target_point = window_to_world_coord(app, xpos, ypos);
    Ray r = {app.eye.position, normalize(Vector3(target_point) - app.eye.position)};
    auto hit = intersect_ray_sphere(r, app.sphere);
    if (!hit.success) {
        return;
    }

    Vector3 point = r.o + hit.t * r.d;

    // Small difference that is not representable in single-prec floats?
    // Should not update orientation then.
    if (point == app_loop.hold_state.start_point) {
        return;
    }

    ch_restart(app_loop.hold_state, point);
    app.reupload_model_mat = true;
    // debug("HELD AND ROTATING");
}

int main() {
    memory_globals::init();
    rng::init_rng(0xdeadbeef);
    {
        App app{};
        app_loop::State app_state{};
        app_loop::run(app, app_state);
    }
    memory_globals::shutdown();
}
