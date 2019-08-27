// This example is a soup of a few things. Most important is switching from
// one framebuffer to other. Second is using shader subroutines. Third is
// using imgui a little. Shader subroutines are a bit awkward though.

#include <learnogl/app_loop.h>
#include <learnogl/eng>
#include <learnogl/math_ops.h>
#include <learnogl/mesh.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/stb_image.h>
#include <scaffold/debug.h>
#include <scaffold/memory.h>
#include <stddef.h> // offsetof
#include <utility>

#include "dbg_imgui.h"
#include <cwd.h>

using namespace math;
using namespace fo;
using namespace mesh;

constexpr int width = 800;
constexpr int height = 600;

void key_cb(GLFWwindow *window, int key, int scancode, int action, int mods);

constexpr char post_vs[] = "/home/snyp/gits/learnogl/test/fb_test/postprocess.vert";
constexpr char post_fs[] = "/home/snyp/gits/learnogl/test/fb_test/postprocess.frag";

constexpr size_t MAX_MESHES_IN_MODEL = 10;

enum PostEffect : int { INVERT = 0, GRAYSCALE, NONE };

struct Gui {
    bool show_test_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = {0.0, 0.0, 0.0, 1.0};
};

struct App {
    unsigned num_meshes;
    GLuint model_vaos[MAX_MESHES_IN_MODEL];          // vao for each mesh
    unsigned model_num_indices[MAX_MESHES_IN_MODEL]; // num vertices in each mesh

    eye::State eye;
    Matrix4x4 view_mat;
    Matrix4x4 proj_mat;
    GLFWwindow *window;

    Gui gui;

    GLuint fbo; // Fb for 'render to a texture'
    GLuint fb_color_tex;
    GLuint fb_depth_rb;

    GLuint quad_vao;

    GLuint program;
    GLuint model_loc;
    GLuint view_loc;
    GLuint proj_loc;

    GLuint post_prog;
    GLuint calc_color_loc;
    GLuint invert_routine_idx;
    GLuint gray_routine_idx;
    GLuint subroutine_idx[1];

    enum PostEffect current_effect = INVERT;

    bool esc_pressed;
};

void init_quad_vao(App &app) {
    // A quad to attach the rendered texture onto
    // clang-format off
    float quad_vert_pos[] = {
        -1.0, -1.0,
        1.0, -1.0,
        1.0,  1.0,
        1.0,  1.0,
        -1.0,  1.0,
        -1.0, -1.0
    };
    // clang-format on

    glGenVertexArrays(1, &app.quad_vao);
    glBindVertexArray(app.quad_vao);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vert_pos), quad_vert_pos, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
}

static bool create_fbo(App &app) {
    glGenFramebuffers(1, &app.fbo);
    glGenTextures(1, &app.fb_color_tex);
    glBindTexture(GL_TEXTURE_2D, app.fb_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* attach the texture to the framebuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);
    glViewport(0, 0, width, height);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.fb_color_tex, 0);

    glGenRenderbuffers(1, &app.fb_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, app.fb_depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, app.fb_depth_rb);

    GLenum draw_bufs[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, draw_bufs);

    log_assert(eng::framebuffer_complete(app.fbo), "Fb incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    log_info("gl error? %08x", glGetError());

    return true;
}

static void set_effect_subroutine(App &app) {
    glUseProgram(app.post_prog);

    GLuint index;

    switch (app.current_effect) {
    case INVERT:
        index = app.invert_routine_idx;
        break;
    case GRAYSCALE:
        index = app.gray_routine_idx;
        break;
    case NONE:
        log_err("Effect value: %i", app.current_effect);
        abort();
    }
    app.subroutine_idx[app.calc_color_loc] = index;
    glUniformSubroutinesuiv(GL_FRAGMENT_SHADER, 1, app.subroutine_idx);
}

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, width, height);
    glfwSetWindowUserPointer(app.window, &app);

    constexpr auto model_file = MODEL_FILE("suzanne.obj");

    imgui::init_imgui(app.window, imgui::InstallCallbacks{false, true, true, true, true});

    auto io = ImGui::GetIO();
    // io.Fonts->AddFontFromFileTTF(TEXTURE_FILE("Lekton-Bold.ttf"), 15.0f);

    Model model(memory_globals::default_allocator(), memory_globals::default_allocator());

    log_assert(load(model, model_file, true), "Failed to load model file: %s", model_file);

    create_fbo(app);
    init_quad_vao(app);

    glfwSetKeyCallback(app.window, key_cb);

    constexpr auto vert_shader_src = R"(
        #version 410

        layout(location = 0) in vec3 vert_pos;
        layout(location = 1) in vec3 vert_normal;
        layout(location = 2) in vec2 vert_tex2d;

        uniform mat4 view_mat, proj_mat;

        out ColorBlock {
            flat vec3 color;
        };

        void main() {
            color = normalize(vert_pos);
            gl_Position = proj_mat * view_mat * vec4(vert_pos, 1.0);
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

    app.program = eng::create_program(vert_shader_src, frag_shader_src);
    app.view_loc = glGetUniformLocation(app.program, "view_mat");
    app.proj_loc = glGetUniformLocation(app.program, "proj_mat");
    glUseProgram(app.program);
    glUniformMatrix4fv(app.view_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&app.view_mat));
    glUniformMatrix4fv(app.proj_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&app.proj_mat));

    // Postprocess shader
    {
        Array<uint8_t> vert_shader_src(memory_globals::default_allocator());
        Array<uint8_t> frag_shader_src(memory_globals::default_allocator());
        read_file(post_vs, vert_shader_src);
        read_file(post_fs, frag_shader_src);
        app.post_prog = eng::create_program((char *)data(vert_shader_src),
                                                (char *)data(frag_shader_src));

        app.calc_color_loc = glGetSubroutineUniformLocation(app.post_prog, GL_FRAGMENT_SHADER, "calc_color");
        app.invert_routine_idx = glGetSubroutineIndex(app.post_prog, GL_FRAGMENT_SHADER, "invert");
        app.gray_routine_idx = glGetSubroutineIndex(app.post_prog, GL_FRAGMENT_SHADER, "gray");

        assert(app.invert_routine_idx != GL_INVALID_INDEX);
        assert(app.gray_routine_idx != GL_INVALID_INDEX);

        GLint n;
        // glGetIntegerv(GL_MAX_SUBROUTINE_UNIFORM_LOCATIONS, &n);
        glGetProgramStageiv(app.post_prog, GL_FRAGMENT_SHADER, GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS, &n);

        log_info("GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS = %i", n);

        assert(n == 1);
        assert(app.calc_color_loc == 0);

        app.subroutine_idx[app.calc_color_loc] = app.invert_routine_idx;

        set_effect_subroutine(app);
    }

    eng::my_clear_color();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    log_info("ERROR? %08x", glGetError());

    glClearColor(app.gui.clear_color.x, app.gui.clear_color.y, app.gui.clear_color.z, app.gui.clear_color.w);

    glEnable(GL_CULL_FACE); // cull face
    glCullFace(GL_BACK);    // cull back face
    glFrontFace(GL_CCW);    // GL_CCW for counter clock-wise

    glViewport(0, 0, width, height);
}

template <> void update<App>(App &app, app_loop::State &app_state) {
    glfwPollEvents();

    imgui::update_imgui();

    {
        static float f = 0.0f;
        ImGui::Text("Hello, world!");
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        ImGui::ColorEdit3("clear color", (float *)&app.gui.clear_color);
        if (ImGui::Button("Test Window"))
            app.gui.show_test_window ^= 1;
        if (ImGui::Button("Another Window"))
            app.gui.show_another_window ^= 1;
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);
    }

    // Eye input
    if (eng::handle_eye_input(app.window, app.eye, app_state.frame_time_in_sec, app.view_mat)) {
        glUseProgram(app.program);
        glUniformMatrix4fv(app.view_loc, 1, GL_FALSE, reinterpret_cast<const float *>(&app.view_mat));
    }

    // Esc
    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    GLuint fbo;
    if (app.current_effect != NONE) {
        fbo = app.fbo;
    } else {
        fbo = 0;
    }

    // glEnable(GL_DEPTH_TEST);
    // glDepthFunc(GL_LESS);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glClearColor(app.gui.clear_color.x, app.gui.clear_color.y, app.gui.clear_color.z, app.gui.clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(app.program);
    // Draw each mesh as usual
    for (unsigned i = 0; i < app.num_meshes; ++i) {
        glBindVertexArray(app.model_vaos[i]);
        glDrawElements(GL_TRIANGLES, app.model_num_indices[i], GL_UNSIGNED_SHORT, 0);
        glBindVertexArray(0);
    }

    // Bind default fb for post-processing. Draw the quad with the just
    // rendered-to texture
    if (app.current_effect != NONE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);   // disable it to make quad fragments always show up
        set_effect_subroutine(app); // Uses the program
        glBindVertexArray(app.quad_vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app.fb_color_tex);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindTexture(GL_TEXTURE_2D, 0);
        glEnable(GL_DEPTH_TEST); // re-enable it.
    }

    // Draw gui
    imgui::render_imgui();

    // Swap buffers
    glfwSwapBuffers(app.window);
}

template <> void close<App>(App &app) {
    imgui::shutdown_imgui();
    glfwTerminate();
}

template <> bool should_close<App>(App &app) { return glfwWindowShouldClose(app.window) || app.esc_pressed; }

} // namespace app_loop

void key_cb(GLFWwindow *window, int key, int scancode, int action, int mods) {
    App *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        app->current_effect = PostEffect((app->current_effect + 1) % 3);
    }

    // Give it to imgui
    imgui::on_key(window, key, scancode, action, mods);
}

int main() {
    memory_globals::init();
    {
        App app{};
        app_loop::State app_state{};
        app_loop::run(app, app_state);
    }
    memory_globals::shutdown();
}
