#include "inc.h"
#include <learnogl/mesh.h>

using namespace fo;
using namespace math;

#ifndef SOURCE_DIR
#error "SOURCE_DIR not defined"
#endif

#define ALLOCATOR memory_globals::default_allocator()

constexpr unsigned MAX_MESHES_IN_MODEL = 10;

struct App {
    int num_particles;
    unsigned num_meshes;

    eye::State eye;
    Matrix4x4 view_mat;
    Matrix4x4 proj_mat;
    float elapsed_time;
    bool esc_pressed;

    GLuint tex_particle_texture;
    GLuint vao_attribs;
    GLuint model_vaos[MAX_MESHES_IN_MODEL];
    unsigned model_num_indices[MAX_MESHES_IN_MODEL];

    GLFWwindow *window;
    int width;
    int height;

    struct {
        GLuint progid;
        struct {
            GLuint mat_view;
            GLuint mat_proj;
            GLuint emitter_pos_wor;
            GLuint current_time;
        } uniloc;
    } prog;

    struct {
        GLuint progid;
        struct {
            GLuint mat_model;
            GLuint mat_view;
            GLuint mat_proj;
        } uniloc;
    } modelprog;

    Vector3 emitter_pos_wor;

    char *image_file;
    char *vs_file;
    char *fs_file;

    App(const char *config_file);

    ~App() {
        free(image_file);
        free(vs_file);
        free(fs_file);
    }
};

constexpr GLuint VELOCITY_LOC = 0;
constexpr GLuint STARTTIME_LOC = 1;

#define PROGID(prog) (prog).progid
#define UNILOC(prog, name) (prog).uniloc.name

static GLboolean g_depth_mask = GL_FALSE;

void key_cb(GLFWwindow *window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        g_depth_mask = g_depth_mask == GL_FALSE ? GL_TRUE : GL_FALSE;
        log_info("Depthmask: %s", g_depth_mask ? "ON" : "OFF");
    }
}

void create_model(App &app) {
    constexpr auto model_file = MODEL_FILE("suzanne.obj");

    using namespace mesh;

    Model model(memory_globals::default_allocator(), memory_globals::default_allocator());

	log_assert(load(model, model_file, true), "Failed to load model file: %s", model_file);

    constexpr auto vert_shader_src = R"(
        #version 410

        layout(location = 0) in vec3 vert_pos;
        layout(location = 1) in vec3 vert_normal;
        layout(location = 2) in vec2 vert_tex2d;

        uniform mat4 modl_mat, view_mat, proj_mat;

        out ColorBlock {
            flat vec3 color;
        };

        void main() {
            color = normalize(vert_normal);
            gl_Position = proj_mat * view_mat * modl_mat * vec4(vert_pos, 1.0);
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

    /*
    for (uint32_t i = 0; i < size(model._mesh_array); ++i) {
        char label[128] = {};
        snprintf(label, sizeof(label), "Model-vao-%u", i);
        log_info("Setting name: %s", label);
        glObjectLabel(GL_VERTEX_ARRAY, app.model_vaos[i], strlen(label), label);
    }
    */

    for (unsigned i = 0; i < size(model._mesh_array); ++i) {
        glBindVertexArray(app.model_vaos[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbos[i]);

        char label[128] = {};
        snprintf(label, sizeof(label), "Model-vao-%u", i);
        log_info("Setting name: %s", label);

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

    PROGID(app.modelprog) = eng::create_program(vert_shader_src, frag_shader_src);
    UNILOC(app.modelprog, mat_model) = glGetUniformLocation(PROGID(app.modelprog), "modl_mat");
    UNILOC(app.modelprog, mat_view) = glGetUniformLocation(PROGID(app.modelprog), "view_mat");
    UNILOC(app.modelprog, mat_proj) = glGetUniformLocation(PROGID(app.modelprog), "proj_mat");
    glUseProgram(PROGID(app.modelprog));

    auto r = versor_from_axis_angle(unit_z, -90.f * one_deg_in_rad);
    auto m = matrix_from_versor(r);

    m.t = m.t + 2.0f * (rotation_about_y(45 * one_deg_in_rad) * Vector4(unit_x, 0.0));

    glUniformMatrix4fv(UNILOC(app.modelprog, mat_model), 1, GL_FALSE, (const float *)&m);
    glUniformMatrix4fv(UNILOC(app.modelprog, mat_view), 1, GL_FALSE, (const float *)&app.view_mat);
    glUniformMatrix4fv(UNILOC(app.modelprog, mat_proj), 1, GL_FALSE, (const float *)&app.proj_mat);
}

void set_particle_draw_state(App &app) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_BLEND);
    glDepthMask(g_depth_mask);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.tex_particle_texture);
    glUseProgram(app.prog.progid);
}

void set_model_draw_state(App &app) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glUseProgram(app.modelprog.progid);
}

App::App(const char *config_file) {
    nfcd_ConfigData *cd = simple_parse_file(config_file, true);

    nfcd_loc root = SIMPLE_MUST(nfcd_root(cd));
    nfcd_loc count = SIMPLE_MUST(nfcd_object_lookup(cd, root, "num_particles"));
    num_particles = (int)nfcd_to_number(cd, count);

    nfcd_loc image_file_loc = SIMPLE_MUST(nfcd_object_lookup(cd, root, "image_file"));
    image_file = strdup(nfcd_to_string(cd, image_file_loc));

    nfcd_loc vs_file_loc = SIMPLE_MUST(nfcd_object_lookup(cd, root, "vert_shader"));
    vs_file = strdup(nfcd_to_string(cd, vs_file_loc));

    nfcd_loc fs_file_loc = SIMPLE_MUST(nfcd_object_lookup(cd, root, "frag_shader"));
    fs_file = strdup(nfcd_to_string(cd, fs_file_loc));

    nfcd_loc emitter_pos = SIMPLE_MUST(nfcd_object_lookup(cd, root, "emitter_position"));
    emitter_pos_wor.x = nfcd_to_number(cd, nfcd_array_item(cd, emitter_pos, 0));
    emitter_pos_wor.y = nfcd_to_number(cd, nfcd_array_item(cd, emitter_pos, 1));
    emitter_pos_wor.z = nfcd_to_number(cd, nfcd_array_item(cd, emitter_pos, 2));

    nfcd_free(cd);

    esc_pressed = false;
    width = 800;
    height = 600;
}

GLuint create_particle_vao(int num_particles) {
    // Starting velocities of each particle along x,y,z
    Array<Vector3> start_velocities(ALLOCATOR);
    Array<float> start_times(ALLOCATOR);

    resize(start_velocities, num_particles);
    resize(start_times, num_particles);

    for (int i = 0; i < num_particles; ++i) {
        Vector3 &v = start_velocities[i];
        v.x = (float)rng::random(-0.5, 0.5);
        v.y = 1.0f;
        v.z = (float)rng::random(-0.5, 0.5);
        start_times[i] = rng::random(0.0, 3.0);
    }

    GLuint velocity_vbo;
    GLuint start_time_vbo;
    glGenBuffers(1, &velocity_vbo);
    glGenBuffers(1, &start_time_vbo);

    glBindBuffer(GL_ARRAY_BUFFER, velocity_vbo);
    glBufferData(GL_ARRAY_BUFFER, num_particles * sizeof(Vector3), data(start_velocities),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, start_time_vbo);
    glBufferData(GL_ARRAY_BUFFER, num_particles * sizeof(float), data(start_times), GL_STATIC_DRAW);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, velocity_vbo);
    glVertexAttribPointer(VELOCITY_LOC, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);
    glBindBuffer(GL_ARRAY_BUFFER, start_time_vbo);
    glVertexAttribPointer(STARTTIME_LOC, 1, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glEnableVertexAttribArray(VELOCITY_LOC);
    glEnableVertexAttribArray(STARTTIME_LOC);

    return vao;
}

void init_program(App &app) {
    Array<uint8_t> vs_src(ALLOCATOR);
    Array<uint8_t> fs_src(ALLOCATOR);
    read_file(app.vs_file, vs_src);
    read_file(app.fs_file, fs_src);
    app.prog.progid = eng::create_program((char *)data(vs_src), (char *)data(fs_src));

    UNILOC(app.prog, mat_view) = glGetUniformLocation(PROGID(app.prog), "mat_view");
    UNILOC(app.prog, mat_proj) = glGetUniformLocation(PROGID(app.prog), "mat_proj");
    UNILOC(app.prog, emitter_pos_wor) = glGetUniformLocation(PROGID(app.prog), "emitter_pos_wor");
    UNILOC(app.prog, current_time) = glGetUniformLocation(PROGID(app.prog), "current_time");
}

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, app.width, app_loop.height);

    eng::enable_debug_output(nullptr, nullptr);

    app.vao_attribs = create_particle_vao(app.num_particles);
    app.tex_particle_texture = load_texture(app.image_file);

    init_program(app);

    log_info(R"(
        Emitter pos: [%f, %f, %f]
        Num particles: %i
    )",
             XYZ(app.emitter_pos_wor), app.num_particles);

    app.eye = eye::toward_negz(3.0);
    app.proj_mat = persp_proj(0.1, 100.0, one_deg_in_rad * 100.0, float(app_loop.height) / app.width);
    eye::update_view_transform(app.eye, app.view_mat);

    glUseProgram(app.prog.progid);
    glUniformMatrix4fv(app.prog.uniloc.mat_view, 1, GL_FALSE, (float *)&app.view_mat);
    glUniformMatrix4fv(app.prog.uniloc.mat_proj, 1, GL_FALSE, (float *)&app.proj_mat);
    glUniform3f(app.prog.uniloc.emitter_pos_wor, app.emitter_pos_wor.x, app.emitter_pos_wor.y,
                app.emitter_pos_wor.z);

    create_model(app);

    glfwSetKeyCallback(app.window, key_cb);
    glViewport(0, 0, app.width, app_loop.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Special stuff for the point raster
    glEnable(GL_PROGRAM_POINT_SIZE);
    glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT);

    log_info("Error?: %08x", glGetError());
}

template <> void update<App>(App &app, app_loop::State &app_state) {
    glfwPollEvents();

    app.elapsed_time = app_state.total_time_in_sec;

    auto rot = versor_from_axis_angle(unit_y, app_state.delta_time_in_sec * 10.0f * one_deg_in_rad);
    app.emitter_pos_wor = apply_versor(rot, app.emitter_pos_wor);

    // Eye input
    if (eng::handle_eye_input(app.window, app.eye, app_state.frame_time_in_sec, app.view_mat)) {
        glUseProgram(app.prog.progid);
        glUniformMatrix4fv(app.prog.uniloc.mat_view, 1, GL_FALSE, reinterpret_cast<float *>(&app.view_mat));

        glUseProgram(app.modelprog.progid);
        glUniformMatrix4fv(app.modelprog.uniloc.mat_view, 1, GL_FALSE,
                           reinterpret_cast<float *>(&app.view_mat));
    }

    // Esc
    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // -- Draw particles
    set_particle_draw_state(app);
    // Source emitter position
    glUniform3f(UNILOC(app.prog, emitter_pos_wor), app.emitter_pos_wor.x, app.emitter_pos_wor.y,
                app.emitter_pos_wor.z);

    // Source current time into shader
    glUniform1f(app.prog.uniloc.current_time, app.elapsed_time / 4.0);
    // VAO
    glBindVertexArray(app.vao_attribs);
    // Draw call
    glDrawArrays(GL_POINTS, 0, app.num_particles);

    // -- Draw model
#if 0
    set_model_draw_state(app);
    for (unsigned i = 0; i < app.num_meshes; ++i) {
        // Mesh VAO
        glBindVertexArray(app.model_vaos[i]);
        // Draw call
        glDrawElements(GL_TRIANGLES, app.model_num_indices[i], GL_UNSIGNED_SHORT, 0);
    }
#endif

    // Swap buffers
    glfwSwapBuffers(app.window);
}

template <> void close<App>(App &app) {
    (void)app;
    eng::close_gl();
    glfwTerminate();
}

template <> bool should_close<App>(App &app) { return glfwWindowShouldClose(app.window) || app.esc_pressed; }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    fo::memory_globals::init();
    rng::init_rng(123);
    {
        app_loop::State app_state{};
        App app(argv[1]);
        app_loop::run(app, app_state);
    }
    fo::memory_globals::shutdown();
}
