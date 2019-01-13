// Visualize a g-buffer. Uses 4 quads, one for each of the attributes.

#include "defer_test_quad_shaders.inc.h"
#include "essentials.h"

#include <cxxopts.hpp>
#include <loguru.hpp>
#include <variant>

using namespace fo;
using namespace math;

constexpr size_t k_max_bars = 500;
constexpr size_t k_bar_uniform_buffer_size = k_max_bars * sizeof(BarWorldTransform);

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

class GBuffer {
  public:
    enum class TextureType : unsigned {
        k_position = 0,
        k_diffuse,
        k_normal,
        k_texcoord,
        k_depth,
    };

    static constexpr size_t k_num_attributes = (size_t)TextureType::k_depth + 1;

    GBuffer() = default;

    void make(int width, int height);

    // Sets the gbuffer as the current write source.
    void set_as_write_framebuffer();

    // Sets the gbuffer as the current read source, and sets `write_fb` as the
    // current write source.
    void set_as_read_framebuffer(GLuint write_fb);

    void set_read_target(TextureType texture_type);

    GLuint _fb;
    std::array<GLuint, k_num_attributes - 1> _textures;
    GLuint _depth_texture;

    enum SetStatus { k_set_as_write, k_set_as_read, k_none };
    SetStatus _set_status;
};

// Impl GBuffer::make
void GBuffer::make(int width, int height) {
    glGenFramebuffers(1, &_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fb);

    glViewport(0, 0, width, height);

    // Create geometry info textures
    glGenTextures(_textures.size(), _textures.data());

    // Create depth texture
    glGenTextures(1, &_depth_texture);

    // Bind textures to the color attachments of the framebuffer. Also create
    // and attach a depth texture.
    for (size_t i = 0; i < _textures.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, _textures[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB32F, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, _textures[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, _depth_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, width, height);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _depth_texture, 0);

    // And need to specify which attachments to draw into (other than the depth
    // attachment, which is always filled)
    std::array<GLenum, k_num_attributes - 1> drawbuffers{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                                         GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(drawbuffers.size(), drawbuffers.data());

    // Just in case
    CHECK_F(eng::framebuffer_complete(_fb), "Framebuffer isn't complete");

    // Restore the default fb (the one glfw creates on startup)
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    _set_status = k_none;
}

void GBuffer::set_as_write_framebuffer() {
    assert(_set_status == k_set_as_read || _set_status == k_none);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fb);
    _set_status = k_set_as_write;
}

void GBuffer::set_as_read_framebuffer(GLuint write_fb) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, write_fb);
    _set_status = k_set_as_read;
}

void GBuffer::set_read_target(TextureType texture_type) {
    assert(_set_status == k_set_as_read);
    glNamedFramebufferReadBuffer(_fb, GL_COLOR_ATTACHMENT0 + static_cast<unsigned>(texture_type));
}

struct GeometryPass {
    GLuint shader_program;
};

struct LightingPass {
    GLuint shader_program;
};

class App {
  public:
    App(std::vector<BarWorldTransform> bar_world_transforms);
    ~App();

    MVP mvp;
    GLuint mvp_ubo;

    std::vector<BarWorldTransform> bar_world_transforms;
    GLuint bar_ubo;

    GLuint bar_vao, bar_vbo, bar_ebo;
    unsigned num_indices;

    eye::State eye;

    GeometryPass geom_pass;
    LightingPass light_pass;

    std::array<GLuint, GBuffer::k_num_attributes> samplepass_progs;

    GBuffer gbuffer;

    std::array<Quad, GBuffer::k_num_attributes>
        quads; // bl, tl, tr, br (pos, diff, norm, st), and mid (diffuse)

    std::array<GLuint, 5> quad_vaos;
    std::array<GLuint, 5> quad_vbos;

    GLFWwindow *window;

    bool draw_aabbs = false;
    bool esc_pressed = false;

    size_t num_bars() const { return bar_world_transforms.size(); }
};

// Impl App::App
App::App(std::vector<BarWorldTransform> bar_world_transforms)
    : bar_world_transforms(std::move(bar_world_transforms)) {}

App::~App() {}

static void make_bar_vao(App &app) {
    par_shapes_mesh *cube = create_mesh();
    // par_shapes_unweld(cube, true);
    par_shapes_compute_normals(cube); // Want proper normals
    shift_par_cube(cube);

    std::vector<VertexData> vertices(cube->npoints);

    for (size_t i = 0; i < cube->npoints; ++i) {
        vertices[i].position.x = cube->points[i * 3];
        vertices[i].position.y = cube->points[i * 3 + 1];
        vertices[i].position.z = cube->points[i * 3 + 2];
        vertices[i].normal.x = cube->normals[i * 3];
        vertices[i].normal.y = cube->normals[i * 3 + 1];
        vertices[i].normal.z = cube->normals[i * 3 + 2];

        // Assign random color to vertex
        vertices[i].st = Vector2{(float)rng::random(0.0, 1.0), (float)rng::random(0.0, 1.0)};
        vertices[i].diffuse = random_vector(0.0, 1.0);
    }

    app.num_indices = cube->ntriangles * 3;

    glGenVertexArrays(1, &app.bar_vao);
    glBindVertexArray(app.bar_vao);

    glGenBuffers(1, &app.bar_vbo);
    glGenBuffers(1, &app.bar_ebo);

    glBindBuffer(GL_ARRAY_BUFFER, app.bar_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(vertices), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.bar_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, app.num_indices * sizeof(uint16_t), cube->triangles,
                 GL_STATIC_DRAW);

    enum : unsigned int { k_pos_loc = 0, k_normal_loc, k_diffuse_loc, k_st_loc };

    // Bindings
    glVertexAttribBinding(k_pos_loc, 0);
    glVertexAttribBinding(k_normal_loc, 0);
    glVertexAttribBinding(k_diffuse_loc, 0);
    glVertexAttribBinding(k_st_loc, 0);

    // Attrib formats
    glVertexAttribFormat(k_pos_loc, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, position));
    glVertexAttribFormat(k_normal_loc, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, normal));
    glVertexAttribFormat(k_diffuse_loc, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, diffuse));
    glVertexAttribFormat(k_st_loc, 2, GL_FLOAT, GL_FALSE, offsetof(VertexData, st));

    // Enable
    glEnableVertexAttribArray(k_pos_loc);
    glEnableVertexAttribArray(k_normal_loc);
    glEnableVertexAttribArray(k_diffuse_loc);
    glEnableVertexAttribArray(k_st_loc);

    // Increase-per-N-instances (all increase per vertex)
    glVertexBindingDivisor(k_pos_loc, 0);
    glVertexBindingDivisor(k_normal_loc, 0);
    glVertexBindingDivisor(k_diffuse_loc, 0);
    glVertexBindingDivisor(k_st_loc, 0);

    // vb binding points
    glBindVertexBuffer(0, app.bar_vbo, 0, sizeof(VertexData));
}

static void make_bar_transforms_ubo(App &app) {
    glGenBuffers(1, &app.bar_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.bar_ubo);
    glBufferData(GL_UNIFORM_BUFFER, vec_bytes(app.bar_world_transforms),
                 app.bar_world_transforms.data(), GL_STATIC_DRAW);

    glBindBufferBase(GL_UNIFORM_BUFFER, 1, app.bar_ubo);
}

struct Viewport {
    Vector2 botleft_window; // Bottom-left corner of viewport (integer always)
    Vector2 extent_window;  // Width and height of viewport (integer always)

    Vector2 depth_range = {0.0f, 1.0f};
};

constexpr Vector2 depth_quad_bl = {-0.3f, -0.3f};
constexpr Vector2 depth_quad_tr = {0.3f, 0.3f};
constexpr Vector2 depth_viewport_extent = {depth_quad_tr.x - depth_quad_bl.x,
                                           depth_quad_tr.y - depth_quad_bl.x};

std::string get_shader_defines(const App &app) {
    // Depth quad corner and width in window space
    constexpr Vector2 depth_quad_bl_window = {k_window_width * (depth_quad_bl.x + 1.0f) / 2.0f,
                                              k_window_height * (depth_quad_bl.y + 1.0f) / 2.0f};

    constexpr Vector2 depth_quad_tr_window = {k_window_width * (depth_quad_tr.x + 1.0f) / 2.0f,
                                              k_window_height * (depth_quad_tr.y + 1.0f) / 2.0f};

    constexpr Vector2 depth_quad_extent = depth_quad_tr_window - depth_quad_bl_window;

    using std::make_pair;
    std::vector<std::pair<const char *, std::variant<int, float>>> defines{
        make_pair("ACTUALLY_USING", 1),
        make_pair("K_NUM_BARS", (int)app.num_bars()),
        make_pair("POSITION_TEXTURE_BINDING", 0),
        make_pair("DIFFUSE_TEXTURE_BINDING", 1),
        make_pair("NORMAL_TEXTURE_BINDING", 2),
        make_pair("TCOORD_TEXTURE_BINDING", 3),
        make_pair("DEPTH_TEXTURE_BINDING", 4),

        make_pair("POSITION_COLOR_ATTACHMENT", 0),
        make_pair("DIFFUSE_COLOR_ATTACHMENT", 1),
        make_pair("NORMAL_COLOR_ATTACHMENT", 2),
        make_pair("TCOORD_COLOR_ATTACHMENT", 3),

        make_pair("MVP_UBO_BINDING", 0),
        make_pair("BAR_TRANSFORMS_UBO_BINDING", 1),

        make_pair("DEPTH_QUAD_BOTLEFT_X", depth_quad_bl_window.x),
        make_pair("DEPTH_QUAD_BOTLEFT_Y", depth_quad_bl_window.y),
        make_pair("DEPTH_QUAD_WIDTH", depth_quad_extent.x),
        make_pair("DEPTH_QUAD_HEIGHT", depth_quad_extent.y)};

    std::string s;
    for (const auto &p : defines) {
        s += "#define ";
        s += p.first;
        s += " ";
        if (p.second.index() == 0) {
            s += std::to_string(std::get<int>(p.second));
        } else {
            s += std::to_string(std::get<float>(p.second));
        }
        s += "\n";
    }
    LOG_F(INFO, "Defines:\n%s", s.c_str());
    return s;
}

namespace app_loop {

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, k_window_width, k_window_height, 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    CHECK_F(confirm_enough_ub_space(app.bar_world_transforms.size()), "Not enough uniform block space");
    {
        GLint n;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &n);
        LOG_F(INFO, "Max fs image units = %i", n);
    }

    // Create programs

    std::string shader_defines = get_shader_defines(app);

    auto create_samplepass_fs = [&shader_defines](const char *shader_source) {
        return eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version,
                                      "#version 430 core\n", GL_FRAGMENT_SHADER, shader_defines.c_str(),
                                      shader_source);
    };

    using eng::create_program;

    GLuint vs =
        eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                               GL_VERTEX_SHADER, shader_defines.c_str(), samplepass_vs);

    app.samplepass_progs[0] = create_program(vs, create_samplepass_fs(samplepass_fs_POS));
    app.samplepass_progs[1] = create_program(vs, create_samplepass_fs(samplepass_fs_DIFFUSE));
    app.samplepass_progs[2] = create_program(vs, create_samplepass_fs(samplepass_fs_NORMAL));
    app.samplepass_progs[3] = create_program(vs, create_samplepass_fs(samplepass_fs_TCOORD));
    app.samplepass_progs[4] = create_program(vs, create_samplepass_fs(samplepass_fs_DEPTH));

    app.geom_pass.shader_program =
        create_vs_fs_prog((fs::path(SOURCE_DIR) / "geometry_pass.vert").u8string().c_str(),
                          (fs::path(SOURCE_DIR) / "geometry_pass.frag").u8string().c_str());

    // Initialize the quads to use as primitives during sampling pass
    app.quads[0] = Quad(Vector2{-1.0, -1.0}, Vector2{0, 0});
    app.quads[1] = Quad(Vector2{-1.0, 0}, Vector2{0, 1.0});
    app.quads[2] = Quad(Vector2{0, 0}, Vector2{1.0, 1.0});
    app.quads[3] = Quad(Vector2{0, -1.0}, Vector2{1.0, 0});
    app.quads[4] = Quad(depth_quad_bl, depth_quad_tr);

    for (size_t i = 0; i < app.quads.size(); ++i) {
        app.quads[i].make_vao(&app.quad_vbos[i], &app.quad_vaos[i]);
    }

    // Initialize MVP uniform
    app.mvp.proj = persp_proj(0.1, 1000.0, 70.0 * one_deg_in_rad, k_aspect_ratio);

    app.eye = eye::toward_negz(2.0f);
    eye::update_view_transform(app.eye, app.mvp.view);

    // Create and initialize mvp ubo
    glGenBuffers(1, &app.mvp_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.mvp_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(MVP), &app.mvp, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, app.mvp_ubo);

    // Make the GBuffer
    app.gbuffer.make(k_window_width, k_window_height);

    // Bar buffers
    make_bar_vao(app);
    make_bar_transforms_ubo(app);

    // Set some basic state
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Disable blending since doesn't make sense for the data we are writing.
    // Moreover the alpha channel of the source data(the stuff we write in the
    // FS)  is going to be defaulted to 0.
    glDisable(GL_BLEND);
}

template <> void update<App>(App &app, State &s) {
    fps_title_update(app.window, s.frame_time_in_sec);
    glfwPollEvents();

    if (eng::handle_eye_input(app.window, app.eye, s.frame_time_in_sec, app.mvp.view)) {
        glBindBuffer(GL_UNIFORM_BUFFER, app.mvp_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MVP), &app.mvp);
    }

    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }

    if (glfwGetKey(app.window, GLFW_KEY_B) == GLFW_PRESS) {
        app.draw_aabbs = true;
    }
}

template <> void render<App>(App &app) {
    // Geometry pass
    glUseProgram(app.geom_pass.shader_program);

    app.gbuffer.set_as_write_framebuffer();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    //      Draw
    glBindVertexArray(app.bar_vao);
    glDrawElementsInstanced(GL_TRIANGLES, app.num_indices, GL_UNSIGNED_SHORT, (const void *)0,
                            app.num_bars());

    // Now bind the g-buffer for reading. This binds write fb slot to the
    // default framebuffer.

    //     Set the gbuffer's fb as the current read fb. This binds the read
    //     slot to the gbuffer's fb, and the default framebuffer as the
    //     current read fb.
    app.gbuffer.set_as_read_framebuffer(0);
    glDrawBuffer(GL_BACK);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.gbuffer._textures[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, app.gbuffer._textures[1]);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, app.gbuffer._textures[2]);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, app.gbuffer._textures[3]);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, app.gbuffer._depth_texture);

    glDisable(GL_DEPTH_TEST);

    for (size_t i = 0; i < GBuffer::k_num_attributes; ++i) {
        glUseProgram(app.samplepass_progs[i]);
        glBindVertexArray(app.quad_vaos[i]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

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
        ("barfile", cxxopts::value<std::string>(), "file containing bar world transforms");
    // clang-format on
    auto result = desc.parse(ac, av);

    CHECK_F(result.count("barfile") == 1, "Must give a file to read the bar data from");

    rng::init_rng();
    fps_title_init("see gbuffer");
    {
        auto bar_world_transforms = read_bar_transforms(result["barfile"].as<std::string>());

        App app(std::move(bar_world_transforms));
        app_loop::State app_state{};
        app_loop::run(app, app_state);
    }
    memory_globals::shutdown();
}
