#include <cwd.h>
#include <glad/glad.h>
#include <learnogl/app_loop.h>
#include <learnogl/eye.h>
#include <learnogl/gl_misc.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/nf_simple.h>
#include <learnogl/rng.h>
#include <scaffold/temp_allocator.h>
#include <learnogl/console.h>

#include <assert.h>

// A test of instancing. We draw a few tets but specify a particular scale
// factor and their position

using namespace fo;
using namespace math;

constexpr float l = 0.5;

Vector3 tet_verts[] = {
    // 0
    {0, l, 0},

    // 1
    {l, -l, l},

    // 2
    {-l, -l, l},

    // 3
    {0, -l, -l},
};

static unsigned short tet_vert_indices[] = {0, 2, 1, 0, 3, 2, 3, 2, 1, 0, 1, 3};

constexpr size_t num_indices = sizeof(tet_vert_indices) / sizeof(unsigned short);

struct TetInfo {
    float scale;
    Vector3 translate;
    Vector3 color;
    Quaternion orientation; // In object frame. This is updated frequently and should be stored together in a
                            // separate client buffer for cache-friendliness
};

void read_tetinfos(fo::Array<TetInfo> &tets) {
    Array<uint8_t> json_src(memory_globals::default_allocator());
    read_file(fs::path(SOURCE_DIR) / "tetinfo.json", json_src);

    auto must = [](nfcd_loc loc, int line) {
        log_assert(loc != nfcd_null(), "Line: %i", line);
        return loc;
    };
#define MUST(loc) must(loc, __LINE__)

    nfcd_ConfigData *cd = nfcd_make(simple_nf_realloc, nullptr, 0, 0);
    const char *ret = nfjp_parse((char *)data(json_src), &cd);
    log_assert(ret == nullptr, "Failed to parse config data - %s", ret);

    int i;

    nfcd_loc root = nfcd_root(cd);
    nfcd_loc tet_array_loc = MUST(nfcd_object_lookup(cd, root, "tets"));
    int num_tets = nfcd_array_size(cd, tet_array_loc);
    debug("Array size = %i\n", num_tets);
    for (int i = 0; i < num_tets; ++i) {
        nfcd_loc tet_object_loc = MUST(nfcd_array_item(cd, tet_array_loc, i));
        nfcd_loc scale_loc = MUST(nfcd_object_lookup(cd, tet_object_loc, "scale"));
        nfcd_loc translate_loc = MUST(nfcd_object_lookup(cd, tet_object_loc, "translate"));
        nfcd_loc color_loc = MUST(nfcd_object_lookup(cd, tet_object_loc, "color"));

        assert(nfcd_array_size(cd, translate_loc) == 3);
        assert(nfcd_array_size(cd, color_loc) == 3);

        float s = nfcd_to_number(cd, scale_loc);

        nfcd_loc tx = nfcd_array_item(cd, translate_loc, 0);
        nfcd_loc ty = nfcd_array_item(cd, translate_loc, 1);
        nfcd_loc tz = nfcd_array_item(cd, translate_loc, 2);

        nfcd_loc r = nfcd_array_item(cd, color_loc, 0);
        nfcd_loc g = nfcd_array_item(cd, color_loc, 1);
        nfcd_loc b = nfcd_array_item(cd, color_loc, 2);

        TetInfo ci{};
        ci.scale = s;

        ci.translate.x = nfcd_to_number(cd, tx);
        ci.translate.y = nfcd_to_number(cd, ty);
        ci.translate.z = nfcd_to_number(cd, tz);

        ci.color.x = nfcd_to_number(cd, r);
        ci.color.y = nfcd_to_number(cd, g);
        ci.color.z = nfcd_to_number(cd, b);

        push_back(tets, ci);
    }

    nfcd_free(cd);

#if 0
    TempAllocator512 ta(memory_globals::default_allocator());
    string_stream::Buffer sb(ta);
    for (const TetInfo &ci : tets) {
        printf("Tet - \n");
        str_of_matrix(ci.ow, sb, false);
        printf("Matrix =\n %s\n\n", string_stream::c_str(sb));
        printf("Color  = [%f %f %f]\n--\n", ci.color.x, ci.color.y, ci.color.z);
        clear(sb);
    }
#endif

#undef MUST
}

constexpr int width = 1024;
constexpr int height = 640;

struct App {
    fo::Array<TetInfo> tets = fo::Array<TetInfo>(memory_globals::default_allocator());
    fo::Array<Vector3> axes = fo::Array<Vector3>(memory_globals::default_allocator());

    console::Console console;

    // Eye
    eye::State eye = eye::toward_negy(2.0);

    float world_angle = 0.0;

    float tet_angle = 0.0; // All tets rotate at the same speed, for now.

    // World frame rotation
    Quaternion world_rot;

    // View transform
    Matrix4x4 view_mat;
    // Projection transform
    Matrix4x4 proj_mat;

    GLuint tet_program;

    // vao
    GLuint tet_vbo, tet_vao;

    GLuint ebo;

    // uniform locations
    GLuint view_loc, proj_loc, world_rot_loc;

    bool esc_pressed = false;

    GLFWwindow *window;
};

static void generate_random_axes(fo::Array<Vector3> &axes) {
    for (size_t i = 0; i < size(axes); ++i) {
        const double theta = rng::random(0, 2 * pi);
        const double psi = rng::random(0, 2 * pi);
        axes[i] = Vector3{float(std::cos(psi) * sin(theta)), float(std::sin(psi) * std::cos(theta)),
                          float(std::cos(theta))};
    }
}

static inline void update_world_rot(App &app, float dt) {
    app.world_angle += dt * (10.f * one_deg_in_rad);
    app.world_rot = versor_from_axis_angle(unit_y, app.world_angle);
}

static inline void update_tet_rots(App &app, float dt) {
    app.tet_angle += dt * (30.f * one_deg_in_rad);
    for (size_t i = 0; i < size(app.axes); ++i) {
        app.tets[i].orientation = versor_from_axis_angle(app.axes[i], app.tet_angle);
    }
}

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, width, height, "instances", 4, 4);
    init(app.console, width, height, 0.7f, 15, 5);

    // read tet info
    read_tetinfos(app.tets);

    debug("Done reading tets");

    resize(app.axes, size(app.tets));

    // generate random rotation axes (model space)
    generate_random_axes(app.axes);

    for (TetInfo &ti : app.tets) {
        ti.orientation = identity_versor;
    }

    glViewport(0, 0, width, height);

    // Shader
    {
        Array<uint8_t> vert_shader_src(memory_globals::default_allocator());
        Array<uint8_t> frag_shader_src(memory_globals::default_allocator());
        read_file(fs::path(SOURCE_DIR) / "instanced_tets_rot.vert", vert_shader_src);
        read_file(fs::path(SOURCE_DIR) / "instanced_tets.frag", frag_shader_src);
        app.tet_program = eng::create_program((char *)data(vert_shader_src),
                                                  (char *)data(frag_shader_src));
    }

    // Init vertex buffers
    glGenBuffers(1, &app.tet_vbo);
    glGenVertexArrays(1, &app.tet_vao);

    glBindVertexArray(app.tet_vao);

    GLuint ebo;
    glGenBuffers(1, &ebo);

    // We will store the vertex positions in the first part of the buffer, and
    // then a list of TetInfo structs for each tet (i.e {color, scale,
    // translate, orientation}
    const size_t total_buffer_size = sizeof(tet_verts) + size(app.tets) * sizeof(TetInfo);

    glBindBuffer(GL_ARRAY_BUFFER, app.tet_vbo);
    glBufferData(GL_ARRAY_BUFFER, total_buffer_size, NULL,
                 GL_STREAM_DRAW); // STREAM_DRAW since the quats will keep changing
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tet_verts), tet_verts);
    glBufferSubData(GL_ARRAY_BUFFER, sizeof(tet_verts), size(app.tets) * sizeof(TetInfo),
                    data(app.tets));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(tet_vert_indices), tet_vert_indices, GL_STATIC_DRAW);

    // Vertex attribute pointer setup
    constexpr GLuint pos_attr_loc = 0;
    constexpr GLuint inst_color_attr_loc = 1;
    constexpr GLuint inst_scaletrans_attr_loc = 2;
    constexpr GLuint inst_orientation_attr_loc = 3;

    glVertexAttribPointer(pos_attr_loc, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    // The next three vert attrib values come from instanced arrays, but we set
    // the attribute pointers the same way, nothing special there.
    glVertexAttribPointer(inst_color_attr_loc, 3, GL_FLOAT, GL_FALSE, sizeof(TetInfo),
                          (void *)(sizeof(tet_verts) + offsetof(TetInfo, color)));

    // Scale and translate are sent together in one vec4 attribute
    glVertexAttribPointer(inst_scaletrans_attr_loc, 4, GL_FLOAT, GL_FALSE, sizeof(TetInfo),
                          (void *)(sizeof(tet_verts) + offsetof(TetInfo, scale))); // Careful =)

    // The model frame orientation is sent in a vec4
    glVertexAttribPointer(inst_orientation_attr_loc, 4, GL_FLOAT, GL_FALSE, sizeof(TetInfo),
                          (void *)(sizeof(tet_verts) + offsetof(TetInfo, orientation)));

    glEnableVertexAttribArray(pos_attr_loc);
    glEnableVertexAttribArray(inst_color_attr_loc);
    glEnableVertexAttribArray(inst_scaletrans_attr_loc);
    glEnableVertexAttribArray(inst_orientation_attr_loc);

    // The instanced attributes' indices should increment after every single
    // instance
    glVertexAttribDivisor(inst_color_attr_loc, 1);
    glVertexAttribDivisor(inst_scaletrans_attr_loc, 1);
    glVertexAttribDivisor(inst_orientation_attr_loc, 1);

    // Setup uniform locs
    app.view_loc = glGetUniformLocation(app.tet_program, "view");
    app.proj_loc = glGetUniformLocation(app.tet_program, "proj");
    app.world_rot_loc = glGetUniformLocation(app.tet_program, "world_rot");

    // Initial uniforms
    eye::update_view_transform(app.eye, app.view_mat);
    app.proj_mat = math::persp_proj(0.1, 100.0, one_deg_in_rad * 100.0, float(width) / height);
    app.world_rot = identity_versor;

    // Source the uniform
    glUseProgram(app.tet_program);
    glUniformMatrix4fv(app.view_loc, 1, GL_FALSE, (float *)&app.view_mat);
    glUniformMatrix4fv(app.proj_loc, 1, GL_FALSE, (float *)&app.proj_mat);
    glUniform4fv(app.world_rot_loc, 1, (float *)&app.world_rot);

    glClearColor(0.26, 0.32, 0.6, 1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}

template <> void update<App>(App &app, app_loop::State &app_state) {
    glfwPollEvents();

    static float prev_fps = app_state.frame_time_in_sec;

    float fps = 1.0f / app_state.frame_time_in_sec;
    fps = 0.2 * prev_fps + 0.8 * fps;

    reset_console_prompt(app.console);
    console_fmt_input(app.console, "FPS: %f", (double)fps);
    add_fmt_string_to_pager(app.console, "%i%% don't give a fuck", rand() % 9000);

    // Eye input
    if (eng::handle_eye_input(app.window, app.eye, app_state.frame_time_in_sec, app.view_mat)) {
        glUseProgram(app.tet_program);
        glUniformMatrix4fv(app.view_loc, 1, GL_FALSE, (float *)&app.view_mat);
    }

    // Esc
    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }

    const float dt = float(app_state.delta_time_in_sec);

    update_world_rot(app, dt);
    update_tet_rots(app, dt);

    glBindBuffer(GL_ARRAY_BUFFER, app.tet_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, sizeof(tet_verts), size(app.tets) * sizeof(TetInfo),
                    data(app.tets));

    glUseProgram(app.tet_program);
    glUniform4fv(app.world_rot_loc, 1, (float *)&app.world_rot);
}

template <> void render<App>(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, width, height);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glUseProgram(app.tet_program);

    // Source world uniform
    glUniform4fv(app.world_rot_loc, 1, (float *)&app.world_rot);

    glBindVertexArray(app.tet_vao);

    // Set each model transform and draw it
    glDrawElementsInstanced(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, 0, size(app.tets));

    // draw_prompt(app.console);
    // draw_pager_updated(app.console);
    // blit_pager(app.console);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Swap buffers
    glfwSwapBuffers(app.window);
}

template <> void close<App>(App &app) { glfwTerminate(); }

template <> bool should_close<App>(App &app) { return glfwWindowShouldClose(app.window) || app.esc_pressed; }

} // namespace app_loop

int main() {
    memory_globals::init();
    rng::init_rng();
    {
        App app;
        app_loop::State state{};
        app_loop::run(app, state);
    }
    memory_globals::shutdown();
}
