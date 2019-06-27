/// Include this file at the very beginning if using OpenGL.
#pragma once

#include <learnogl/start.h>

#include <GLFW/glfw3.h>
#include <fmt/format.h>
#include <learnogl/colors.h>
#include <learnogl/eye.h>
#include <learnogl/file_monitor.h>
#include <learnogl/gl_binding_state.h>
#include <learnogl/input_handler.h>
#include <learnogl/mesh.h>
#include <learnogl/nf_simple.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <learnogl/audio.h>
#include <learnogl/fixed_string_buffer.h>
#include <learnogl/resource.h>
#include <learnogl/rng.h>
#include <learnogl/shader.h>
#include <learnogl/string_table.h>
#include <learnogl/typed_gl_resources.h>
#include <scaffold/array.h>
#include <scaffold/bitflags.h>
#include <scaffold/math_types.h>
#include <scaffold/temp_allocator.h>

#include <memory>
#include <sstream>
#include <unordered_map>

namespace eng {

constexpr int LEARNOGL_GL_MAJOR_VERSION = 4;
constexpr int LEARNOGL_GL_MINOR_VERSION = 5;

constexpr bool USE_DEBUG_SYNCHRONOUS = true; // Don't touch this

/// not much
constexpr inline GLboolean glbool(bool b) { return b ? GL_TRUE : GL_FALSE; }

/// Type of debug callback
using DebugCallbackFn = void (*)(GLenum source,
                                 GLenum type,
                                 GLuint id,
                                 GLenum severity,
                                 GLsizei length,
                                 const GLchar *message,
                                 const void *user_data);

struct NonGLGlobalsConfig {
    u64 scratch_buffer_size = 1024;
    u64 average_string_length = 15;
    u64 num_unique_strings = 50;
    bool init_file_monitor = true;
    u32 rng_seed = 0xdeadbeef;
};

// A storage for commonly used meshes
struct ShapeMeshes {
    mesh::StrippedMeshData mesh_data;

    GLuint pos_normal_uv_vao;
    GLuint pos2d_vao;

    GLuint mesh_vbo;
    GLuint mesh_ebo;

    // Cube, Sphere, Plane all three a radius (or half extent) of 1.0, and centered at origin.

    // VBO offset is in terms of bytes (to use with BindVertexBuffer)
    u32 cube_vbo_offset;
    u32 sphere_vbo_offset;
    u32 plane_vbo_offset;
    u32 screen_quad_vbo_offset;

    // EBO offset is also in terms of bytes (the `indices` parameter) of DrawElements
    u32 cube_ebo_offset;
    u32 sphere_ebo_offset;
    u32 plane_ebo_offset;
    u32 screen_quad_ebo_offset;

    // Number of vertices in each mesh. Only sphere will change depending on the number of slices and stacks.
    u32 num_cube_vertices;
    u32 num_sphere_vertices;
    u32 num_plane_vertices;
    u32 num_screen_quad_vertices;
};

// The number of threads in x and y axes is set to the max power of 2 that D3D10 allows. For z, it's only 1.
#ifndef LOGL_MAX_THREADS_PER_GROUP
#    define LOGL_MAX_THREADS_PER_GROUP 512
#endif

// Storage for common GL capabilites
struct GLCaps {
    u32 max_uniform_block_size;
    u32 max_texture_image_units;
    u32 max_uniform_buffer_bindings;
    f32 max_anisotropy;

    i64 max_compute_groups[3];
    i64 max_threads_per_group[3];
    i64 max_compute_shared_mem_size;
};

const GLCaps &caps();

/// Only notifications of these severities will be logged
struct DebugCallbackSeverity {
    bool high;
    bool medium;
    bool low;
    bool notification;
};

struct StartGLParams {
    unsigned int msaa_samples = 4;
    unsigned int major_version = LEARNOGL_GL_MAJOR_VERSION;
    unsigned int minor_version = LEARNOGL_GL_MINOR_VERSION;
    bool gl_forward_compat = false;
    const char *window_title = "No window title";
    unsigned int window_width = 1024;
    unsigned int window_height = 768;
    bool enable_debug_output = true;
    DebugCallbackSeverity debug_callback_severity = { true, true, false, false };
    bool abort_on_error = true;
    bool double_buffer_enabled = true;
    bool window_resize_enabled = false;
    bool wait_refresh_before_swap = true;
    bool load_renderdoc = false;
    const char *rdoc_capture_template = nullptr; // Default value of nullptr denotes LOGL_SOURCE_DIR/captures
    fo::Vector4 clear_color = colors::AliceBlue;
    const char *mild_output_logfile = nullptr; // nullptr denotes stderr
};

/// Container for all global stuff.
struct GLApp {
    GLFWwindow *window = nullptr;
    StringTable string_table{ 10, 30 };
    BindingState bs;
    FileMonitor file_monitor;
    // ShapeMeshes shape_meshes;
    // ResourceManager res_man;
    // SoundManager sound_man;
    RenderManager render_manager;
    FBO default_fbo;

    inistorage::Storage config_ini;

    // For all my fixed string needs
    FixedStringBuffer fixed_string_buffer;
};

/// Returns ref to a global GLApp structure which gets initialized by start_gl by default
GLApp &gl();

// inline ResourceManager &g_res_man() { return gl().res_man; }

inline BindingState &g_bs() { return gl().bs; }

// inline SoundManager &g_sound_man() { return gl().sound_man; }

inline StringTable &g_st() { return gl().string_table; }

inline RenderManager &g_rm() { return gl().render_manager; }

inline inistorage::Storage &g_ini() { return gl().config_ini; }

inline GLuint gluint_from_globjecthandle(const GLObjectHandle &handle) {
    return eng::get_gluint_from_rmid(g_rm(), handle.rmid());
}

inline FixedStringBuffer &g_strings() { return gl().fixed_string_buffer; }

/// Creates a window and initializes a GL context
void start_gl(const StartGLParams &params, GLApp &gl_app = gl());

/// Frees GL resources from given `gl_app`.
void close_gl(const StartGLParams &params, GLApp &gl_app = gl());

inline void update_camera_block(const Camera &c, PerCameraUBlockFormat &per_camera) {
    per_camera.view_from_world_xform = c.view_xform();
    per_camera.clip_from_view_xform = c.proj_xform();
    per_camera.eye_position = fo::Vector4(c.position(), 1.0f);
}

/// Enables debugging using the ARB_debug_output facilities. Use nullptr for default callback defined in the
/// .cpp file.
void enable_debug_output(DebugCallbackFn debug_callback = nullptr,
                         void *user_data = nullptr,
                         bool abort_on_error = false,
                         DebugCallbackSeverity debug_callback_severity = DebugCallbackSeverity{
                             true, true, true, false });

// Create a new uniform buffer of given size.
inline GLuint create_uniform_buffer(size_t size, GLenum usage = GL_DYNAMIC_DRAW) {
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, size, nullptr, usage);
    return buffer;
}

bool handle_camera_input(GLFWwindow *window, eng::Camera &camera, float dt);

bool handle_eye_input(GLFWwindow *window, eye::State &e, float dt, fo::Matrix4x4 &view_mat);

bool handle_eye_input_from_callback(GLFWwindow *window,
                                    const input::OnKeyArgs &args,
                                    eye::State &e,
                                    fo::Matrix4x4 &view_mat);

// # Quick static meshes. Copies par mesh into our staple mesh::Model data structure. So kinda redundant work.
// Also these do not have tangent vectors as attribute.

// Loads a cube mesh. The cube ranges from [-0.5, 0.5] across all the 3 dimensions, i.e. the extent is 1 along
// each axis.
void load_cube_mesh(mesh::Model &model,
                    const fo::Matrix4x4 &transform = math::identity_matrix,
                    bool create_normals = false,
                    bool dummy_texcoords = false);

// Load a sphere mesh.
void load_sphere_mesh(mesh::Model &model,
                      int slices = 5,
                      int stacks = 5,
                      const fo::Matrix4x4 &transform = math::identity_matrix);

// Load a plane mesh. The plane ranges from [-0.5, 0.5] along the x and y axes. z axis is 0 for each vertex.
void load_plane_mesh(mesh::Model &model, const fo::Matrix4x4 &transform = math::identity_matrix);

inline void draw_full_screen_quad(GLuint no_attrib_vao = gl().bs.no_attrib_vao()) {
    glBindVertexBuffer(0, 0, 0, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(no_attrib_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Tracking per-frame resource storage (upto about 3 frames) can be made easier using this struct.
template <typename Int = i16> struct ClonedResourceState {
    GETSET(Int, num_clones);
    GETSET(Int, server_clone);
    GETSET(Int, client_clone);

    ClonedResourceState() = default;

    ClonedResourceState(Int num_clones)
        : _num_clones(num_clones)
        , _server_clone(-Int(num_clones))
        , _client_clone(0) {}

    void reset() {
        _server_clone = -Int(_num_clones);
        _client_clone = 0;
    }

    bool server_slot_accessible() const { return _server_clone >= 0; }

    // Client is done with a frame
    void set_client_done() {
        _client_clone = (_client_clone + 1) % _num_clones;
        _server_clone = (_server_clone + 1) % _num_clones;
    }
};

struct VaoFloatFormat {
    GLuint location;
    GLint components;
    GLenum type;
    GLboolean normalized;
    GLuint relative_offset;

    VaoFloatFormat() = default;

    VaoFloatFormat(
        GLuint location, GLuint components, GLenum type, GLboolean normalized, GLuint relative_offset)
        : location(location)
        , components(components)
        , type(type)
        , normalized(normalized)
        , relative_offset(relative_offset) {}
};

// Quickly write all the info needed to create a vao. All attributes are sourced from vertex buffer binding
// point 0.
inline GLuint gen_vao(std::initializer_list<VaoFloatFormat> formats);

inline GLuint gen_vao(std::initializer_list<VaoFloatFormat> attrib_formats) {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    for (const auto &f : attrib_formats) {
        glVertexAttribFormat(f.location, f.components, f.type, f.normalized, f.relative_offset);
        glEnableVertexAttribArray(f.location);
        glVertexAttribBinding(f.location, 0);
    }
    return vao;
}

inline std::string glsl_vec_string(const fo::Vector3 &v) {
    return fmt::format("vec3({}, {}, {})", v.x, v.y, v.z);
}

inline std::string glsl_vec_string(const fo::Vector4 &v) {
    return fmt::format("vec4({}, {}, {}, {})", v.x, v.y, v.z, v.w);
}

inline std::string glsl_vec_string(const fo::Vector2 &v) { return fmt::format("vec3({}, {})", v.x, v.y); }

} // namespace eng
