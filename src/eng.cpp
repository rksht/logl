// Include glad first since we must include GLFW ourselves in this file.
#include <glad/glad.h>

#if defined(WIN32)
#    include <windows.h>
#    define GLFW_EXPOSE_NATIVE_WGL
#    define GLFW_EXPOSE_NATIVE_WIN32

#    define GLFWAPI __declspec(dllimport)

#elif defined(__linux__)
#    include <dlfcn.h>
#    define GLFW_EXPOSE_NATIVE_GLX
#    define GLFW_EXPOSE_NATIVE_X11

// #    define GLFWAPI __attribute__((visibility("default")))

#else
#    warning "Unknown platform"

#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <learnogl/callstack.h>
#include <learnogl/eng.h>
#include <learnogl/math_ops.h>
#include <learnogl/par_shapes.h>
#include <learnogl/renderdoc_app.h>
#include <learnogl/shader.h>
#include <learnogl/string_table.h>
#include <scaffold/debug.h>
#include <scaffold/string_stream.h>
#include <scaffold/temp_allocator.h>

#include <new>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

using namespace fo;
using namespace eng::math;

std::aligned_storage_t<sizeof(eng::GLCaps)> caps_storage[1];

static inline eng::GLCaps *caps_storage_ptr() { return reinterpret_cast<eng::GLCaps *>(caps_storage); }

static void init_caps() {
    GLint single_int;
    GLfloat single_float;

    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &single_int);
    caps_storage_ptr()->max_uniform_block_size = (u32)single_int;

    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &single_int);
    caps_storage_ptr()->max_texture_image_units = (u32)single_int;

    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &single_int);
    caps_storage_ptr()->max_uniform_buffer_bindings = (u32)single_int;

    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &single_float);
    caps_storage_ptr()->max_anisotropy = single_float;

    VLOG_F(LOGL_MILD_LOG_CHANNEL, "max_anisotropy = %.2f", single_float);

    i64 max_compute_groups[3];
    i64 max_threads_per_group[3];

    glGetInteger64i_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &max_compute_groups[0]);
    glGetInteger64i_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &max_compute_groups[1]);
    glGetInteger64i_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &max_compute_groups[2]);

    glGetInteger64i_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &max_threads_per_group[0]);
    glGetInteger64i_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &max_threads_per_group[1]);
    glGetInteger64i_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &max_threads_per_group[2]);

    std::copy(max_compute_groups, max_compute_groups + 3, caps_storage_ptr()->max_compute_groups);
    std::copy(max_threads_per_group, max_threads_per_group + 3, caps_storage_ptr()->max_threads_per_group);

    VLOG_F(LOGL_MILD_LOG_CHANNEL,
           "GL_MAX_COMPUTE_WORK_GROUP_COUNT = [%li, %li, %li]",
           max_compute_groups[0],
           max_compute_groups[1],
           max_compute_groups[2]);

    VLOG_F(LOGL_MILD_LOG_CHANNEL,
           "GL_MAX_COMPUTE_WORK_GROUP_SIZE = [%li, %li, %li]",
           max_threads_per_group[0],
           max_threads_per_group[1],
           max_threads_per_group[2]);

    i32 max_compute_shared_mem_size = 0;
    glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &max_compute_shared_mem_size);
    caps_storage_ptr()->max_compute_shared_mem_size = max_compute_shared_mem_size;
    LOG_F(INFO, "GL_MAX_COMPUTE_SHARED_MEMORY_SIZE = %i", max_compute_shared_mem_size);

    CHECK_LE_F(LOGL_MAX_THREADS_PER_GROUP, max_threads_per_group[0]);
    CHECK_LE_F(LOGL_MAX_THREADS_PER_GROUP, max_threads_per_group[1]);
}

namespace eng {

GLApp::GLApp(u32 scene_tree_node_pool_size)
    : scene_tree(scene_tree_node_pool_size) {}

std::aligned_storage_t<sizeof(GLApp)> _gl_storage[1];

GLApp &gl() { return *reinterpret_cast<GLApp *>(_gl_storage); }

struct DebugMessageBuffer {
    DebugCallbackSeverity debug_callback_severity;
    TempAllocator512 ta{ memory_globals::default_allocator() }; // Buffer for message strings
    bool abort_on_error = true;
    bool dont_abort_on_shader_compile_error = true;
};

std::aligned_storage_t<sizeof(DebugMessageBuffer), alignof(DebugMessageBuffer)> debug_message_buffer[1];

static void default_debug_callback(GLenum source,
                                   GLenum type,
                                   GLuint id,
                                   GLenum severity,
                                   GLsizei length,
                                   const GLchar *message,
                                   const void *user_data) {

    (void)user_data;

    using namespace string_stream;

    DebugMessageBuffer *dmb = reinterpret_cast<DebugMessageBuffer *>(&debug_message_buffer[0]);

    if ((!dmb->debug_callback_severity.notification && severity == GL_DEBUG_SEVERITY_NOTIFICATION) ||
        (!dmb->debug_callback_severity.high && severity == GL_DEBUG_SEVERITY_HIGH) ||
        (!dmb->debug_callback_severity.medium && severity == GL_DEBUG_SEVERITY_MEDIUM) ||
        (!dmb->debug_callback_severity.low && severity == GL_DEBUG_SEVERITY_LOW)) {
        return;
    }

    // Create a string buffer
    Buffer msg(dmb->ta);

#define CASE_APPEND(x)                                                                                       \
    case GL_DEBUG_##x:                                                                                       \
        msg << #x;                                                                                           \
        break

    msg << "Source: ";

    switch (source) {
        CASE_APPEND(SOURCE_APPLICATION);
        CASE_APPEND(SOURCE_THIRD_PARTY);
        CASE_APPEND(SOURCE_SHADER_COMPILER);
        CASE_APPEND(SOURCE_API);
        CASE_APPEND(SOURCE_WINDOW_SYSTEM);
        CASE_APPEND(SOURCE_OTHER);
    }

    msg << ", Type: ";

    switch (type) {
        CASE_APPEND(TYPE_ERROR);
        CASE_APPEND(TYPE_DEPRECATED_BEHAVIOR);
        CASE_APPEND(TYPE_UNDEFINED_BEHAVIOR);
        CASE_APPEND(TYPE_PORTABILITY);
        CASE_APPEND(TYPE_PERFORMANCE);
        CASE_APPEND(TYPE_MARKER);
        CASE_APPEND(TYPE_PUSH_GROUP);
        CASE_APPEND(TYPE_POP_GROUP);
        CASE_APPEND(TYPE_OTHER);
    }

    msg << ", Severity: ";
    switch (severity) {
        CASE_APPEND(SEVERITY_HIGH);
        CASE_APPEND(SEVERITY_MEDIUM);
        CASE_APPEND(SEVERITY_LOW);
        CASE_APPEND(SEVERITY_NOTIFICATION);
    }

    msg << "\nMessage:\n-----\n";
    if (length < 0) {
        msg << message;
    } else {
        printf(msg, "%.*s\n-----\n", length, message);
    }

    loguru::NamedVerbosity verbosity = loguru::NamedVerbosity::Verbosity_1;

    if (severity == GL_DEBUG_SEVERITY_MEDIUM || severity == GL_DEBUG_SEVERITY_HIGH) {
        verbosity = LOGL_ERROR_LOG_CHANNEL;
    } else if (severity == GL_DEBUG_SEVERITY_LOW) {
        verbosity = LOGL_MILD_LOG_CHANNEL;
    }

    VLOG_F(verbosity, "GL_DEBUG_OUTPUT: %s\n", c_str(msg));

#undef CASE_APPEND

    if (dmb->abort_on_error && severity == GL_DEBUG_SEVERITY_HIGH) {
        if (source == GL_DEBUG_SOURCE_SHADER_COMPILER && dmb->dont_abort_on_shader_compile_error) {
            VLOG_F(LOGL_ERROR_LOG_CHANNEL, "Shader compiler error");
            return;
        }

        string_stream::Buffer ss(memory_globals::default_allocator());
        print_callstack(ss);

        ABORT_F("GL_DEBUG_SEVERITY_HIGH - callstack - %s", string_stream::c_str(ss));
    }
}

void enable_debug_output(DebugCallbackFn debug_callback,
                         void *userdata,
                         bool abort_on_error,
                         DebugCallbackSeverity debug_callback_severity) {
    GLint context_flags;
    glGetIntegerv(GL_CONTEXT_FLAGS, &context_flags);

    if (context_flags & GL_DEBUG_OUTPUT) {
        log_info("GL_DEBUG_OUTPUT is already enabled");
        return;
    } else {
        log_info("Enabling GL_DEBUG_OUTPUT");
    }

    glEnable(GL_DEBUG_OUTPUT);

    if (USE_DEBUG_SYNCHRONOUS) {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    } else {
        glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }

    if (debug_callback != nullptr) {
        glDebugMessageCallback(debug_callback, userdata);
    } else {
        glDebugMessageCallback(default_debug_callback, nullptr);
    }

    DebugMessageBuffer *dmb = reinterpret_cast<DebugMessageBuffer *>(&debug_message_buffer[0]);
    dmb->debug_callback_severity = debug_callback_severity;
    dmb->abort_on_error = abort_on_error;
}

// Fwd
TU_LOCAL void init_shape_mesh_struct(ShapeMeshes &shape_meshes);

// Fwd
TU_LOCAL void dont_load_renderdoc();

// Fwd
TU_LOCAL void load_app_config(inistorage::Storage &config_ini);

void start_gl(const StartGLParams &params, GLApp &gl_app) {
    CHECK_EQ_F(&gl_app, &gl());

    LOG_F(INFO, "Starting OpenGL...");
    CHECK_F(glfwInit() != 0, "Failed to init GLFW");
    if (params.msaa_samples == 0) {
        LOG_F(INFO, "Disabling multisampling");
    } else {
        LOG_F(INFO, "Enabling multisampling");
    }
    glfwWindowHint(GLFW_SAMPLES, (int)params.msaa_samples);

    glfwWindowHint(GLFW_DOUBLEBUFFER, params.double_buffer_enabled ? 1 : 0);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, params.major_version);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, params.minor_version);
    glfwWindowHint(GLFW_RESIZABLE, params.window_resize_enabled ? GL_TRUE : GL_FALSE);

    if (params.mild_output_logfile != nullptr) {
        loguru::add_file(params.mild_output_logfile, loguru::FileMode::Truncate, LOGL_MILD_LOG_CHANNEL);
    }

#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    if (params.gl_forward_compat) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    } else {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    // Initializing debug message buffer
    new (debug_message_buffer) DebugMessageBuffer{};

    // Create window
    GLFWwindow *window = glfwCreateWindow(
        (int)params.window_width, (int)params.window_height, params.window_title, nullptr, nullptr);
    CHECK_NE_F(window, nullptr, "Could not create window");

    glfwMakeContextCurrent(window);

    if (params.wait_refresh_before_swap) {
        glfwSwapInterval(1);
    } else {
        glfwSwapInterval(0);
    }

    CHECK_NE_F(gladLoadGL(), 0, "Failed to load GL using glad...");

    const auto renderer = glGetString(GL_RENDERER);
    const auto version = glGetString(GL_VERSION);

    LOG_F(INFO, "OpenGL renderer: %s, version: %s", renderer, version);

    // Store the capabilities info
    init_caps();

    if (params.enable_debug_output) {
        enable_debug_output(nullptr, nullptr, params.abort_on_error, params.debug_callback_severity);
    }

    inistorage::Storage config_ini;
    load_app_config(config_ini);

    if (params.ac != 0) {
        CHECK_NE_F(params.av, nullptr);

        inistorage::Storage cmdline_ini;
        Error err = cmdline_ini.init_from_args(params.ac, params.av);
        if (err) {
            ABORT_F("%s", err.to_string());
        }

        config_ini.merge(cmdline_ini);
    }

    u32 scene_node_pool_size = 0;
    INI_STORE_DEFAULT("scene_node_pool_size", config_ini.boolean, scene_node_pool_size, 100);

    // Ctor the GLApp struct
    new (&gl_app) GLApp(scene_node_pool_size);

    gl_app.config_ini = std::move(config_ini);

    gl_app.window = window;

    if (params.load_renderdoc) {
        load_renderdoc(params.rdoc_capture_template);
    } else {
        dont_load_renderdoc();
    }

    // init_sound_manager(gl_app.sound_man, gl_app.window);

    // Init GL binding state manager
    gl_app.bs.init(BindingStateConfig());

    // Init the shape meshes
    // init_shape_mesh_struct(gl_app.shape_meshes);

    {
        eng::ShaderGlobalsConfig shaders_config;

        if (!gl().config_ini.is_empty()) {
            INI_STORE_DEFAULT("print_preprocessed_shader",
                              gl().config_ini.boolean,
                              shaders_config.print_after_preprocessing,
                              false);
        }

        // Initialize shader globals.
        init_shader_globals(shaders_config);
        add_shader_search_path(LOGL_SHADERS_DIR);
    }

    gl_app.default_fbo.init_from_default_framebuffer();

    gl_app.window_size = Vec2((f32)params.window_width, (f32)params.window_height);

    // Initialize the camera
    gl_app.camera.look_at(params.camera_position, params.camera_look_at, params.camera_up, true);

    // Store the window size
    gl_app.camera.set_proj(
        0.2f, 1000.0f, 70.0f * one_deg_in_rad, gl_app.window_size.x / gl_app.window_size.y);

    init_render_manager(gl_app.render_manager);

    glClearColor(XYZW(params.clear_color));
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(gl_app.window);
}

void close_gl(const StartGLParams &params, GLApp &gl_app) {
    // Close shader globals before anything
    close_shader_globals();

    DebugMessageBuffer *dmb = (DebugMessageBuffer *)debug_message_buffer;
    dmb->~DebugMessageBuffer();

    if (params.load_renderdoc) {
        shutdown_renderdoc();
    }

    // @rksht : I really don't like this. AT ALL. If some things are going to be global, just have them
    // global.
    if (&gl_app == &gl()) {
        gl().~GLApp();
    }

    glfwTerminate();
}

const GLCaps &caps() { return *reinterpret_cast<const GLCaps *>(caps_storage); }

bool handle_eye_input(GLFWwindow *window, eye::State &e, float dt, fo::Matrix4x4 &view_mat) {
    constexpr float eye_linear_vel = 20.0f;
    constexpr float eye_angular_vel = 40.0f * math::one_deg_in_rad;

    bool moved = false;

    if (glfwGetKey(window, GLFW_KEY_W)) {
        e.position = e.position + (eye_linear_vel * dt) * eye::forward(e);
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S)) {
        e.position = e.position - (eye_linear_vel * dt) * eye::forward(e);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_A)) {
        e.position = e.position - (eye_linear_vel * dt) * eye::right(e);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_D)) {
        e.position = e.position + (eye_linear_vel * dt) * eye::right(e);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_Y)) {
        e.position = e.position + (eye_linear_vel * dt) * eye::up(e);
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_H)) {
        e.position = e.position - (eye_linear_vel * dt) * eye::up(e);
        moved = true;
    }

    // These ones rotate about the eye_up axis (yaw)
    if (glfwGetKey(window, GLFW_KEY_LEFT)) {
        eye::yaw(e, eye_angular_vel * dt);
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT)) {
        eye::yaw(e, -eye_angular_vel * dt);
        moved = true;
    }
    // These keys rotate the eye about the res.eye_right axis  (pitch)
    if (glfwGetKey(window, GLFW_KEY_UP)) {
        eye::pitch(e, eye_angular_vel * dt);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_DOWN)) {
        eye::pitch(e, -eye_angular_vel * dt);
        moved = true;
    }
    // These keys rotate the eye about the res.eye_fwd axis (roll)
    if (glfwGetKey(window, GLFW_KEY_J)) {
        eye::roll(e, eye_angular_vel * dt);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_L)) {
        eye::roll(e, -eye_angular_vel * dt);
        moved = true;
    }

    if (moved) {
        eye::update_view_transform(e, view_mat);
    }

    return moved;
}

bool handle_camera_input(GLFWwindow *window, eng::Camera &camera, float dt) {
    handle_eye_input(window, camera._eye, dt, camera._view_xform);
}

bool handle_eye_input_from_callback(GLFWwindow *window,
                                    const input::OnKeyArgs &args,
                                    eye::State &e,
                                    Matrix4x4 &view_mat) {
    constexpr float eye_linear_vel = 4.0f;
    constexpr float eye_angular_vel = 40.0f * math::one_deg_in_rad;

    if (args.action == GLFW_RELEASE) {
        return false;
    }

    bool moved = false;

    const float dt = float(16e-3);

    switch (args.key) {
    case GLFW_KEY_W: {
        e.position = e.position + (eye_linear_vel * dt) * eye::forward(e);
        moved = true;
    } break;
    case GLFW_KEY_S: {
        e.position = e.position - (eye_linear_vel * dt) * eye::forward(e);
        moved = true;
    } break;

    case GLFW_KEY_A: {
        e.position = e.position - (eye_linear_vel * dt) * eye::right(e);
        moved = true;
    } break;
    case GLFW_KEY_D: {
        e.position = e.position + (eye_linear_vel * dt) * eye::right(e);
        moved = true;
    } break;

    case GLFW_KEY_Y: {
        e.position = e.position + (eye_linear_vel * dt) * eye::up(e);
        moved = true;
    } break;
    case GLFW_KEY_H: {
        e.position = e.position - (eye_linear_vel * dt) * eye::up(e);
        moved = true;
    } break;

    // These ones rotate about the eye_up axis (yaw)
    case GLFW_KEY_LEFT: {
        eye::yaw(e, eye_angular_vel * dt);
        moved = true;
    } break;
    case GLFW_KEY_RIGHT: {
        eye::yaw(e, -eye_angular_vel * dt);
        moved = true;
    } break;
    // These keys rotate the eye about the res.eye_right axis  (pitch)
    case GLFW_KEY_UP: {
        eye::pitch(e, eye_angular_vel * dt);
        moved = true;
    } break;

    case GLFW_KEY_DOWN: {
        eye::pitch(e, -eye_angular_vel * dt);
        moved = true;
    } break;
    // These keys rotate the eye about the res.eye_fwd axis (roll)
    case GLFW_KEY_J: {
        eye::roll(e, eye_angular_vel * dt);
        moved = true;
    } break;

    case GLFW_KEY_L: {
        eye::roll(e, -eye_angular_vel * dt);
        moved = true;
    } break;

    default:
        break;
    }

    if (moved) {
        eye::update_view_transform(e, view_mat);
    }

    return moved;
}

static inline void shift_par_cube(par_shapes_mesh *cube) {
    for (int i = 0; i < cube->npoints; ++i) {
        cube->points[i * 3] -= 0.5f;
        cube->points[i * 3 + 1] -= 0.5f;
        cube->points[i * 3 + 2] -= 0.5f;
    }
}

static inline void make_unit_radius_cube(par_shapes_mesh *cube) {
    fn_ transform = [](float p) { return p * 2.0f - 1.0f; };
    for (int i = 0; i < cube->npoints; ++i) {
        cube->points[i * 3] = transform(cube->points[i * 3]);
        cube->points[i * 3 + 1] = transform(cube->points[i * 3 + 1]);
        cube->points[i * 3 + 2] = transform(cube->points[i * 3 + 2]);
    }
}

void load_cube_mesh(mesh::Model &m,
                    const fo::Matrix4x4 &transform,
                    bool create_normals,
                    bool dummy_texcoords) {
    par_shapes_mesh *p = par_shapes_create_cube();

    DEFERSTAT(par_shapes_free_mesh(p));

    par_shapes_unweld(p, true);
    par_shapes_compute_normals(p);

    // Using a [-1, 1] box breaks a sample or two. Be wary.
#if 0

    shift_par_cube(p);
#else

    make_unit_radius_cube(p);
#endif

    // Push a mesh struct
    push_back(m._mesh_array, {});

    const size_t packed_attr_size =
        sizeof(Vector3) + (create_normals ? sizeof(Vector3) : 0) + (dummy_texcoords ? sizeof(Vector2) : 0);
    const size_t vertex_buffer_size = packed_attr_size * p->npoints;
    const size_t index_buffer_size = p->ntriangles * 3 * sizeof(u16);

    // Init the mesh struct
    auto &md = m._mesh_array[0];
    md.buffer = (uint8_t *)m._buffer_allocator->allocate(vertex_buffer_size + index_buffer_size);

    md.o.packed_attr_size = (u32)packed_attr_size;
    md.o.num_vertices = p->npoints;
    md.o.num_faces = p->ntriangles;

    md.o.position_offset = 0;
    md.o.normal_offset = create_normals ? sizeof(Vector3) : mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.tex2d_offset = dummy_texcoords ? sizeof(Vector3) * 2 : mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;

    // If we want to create normals, we use the direction vector from center of the cube to the vertex

    Vector3 center = { 0.0f, 0.0f, 0.0f };

    for (u32 i = 0; i < md.o.num_vertices; ++i) {
        auto pos = reinterpret_cast<Vector3 *>(md.buffer + i * md.o.packed_attr_size);
        memset(pos, 0, md.o.packed_attr_size);
        pos->x = p->points[i * 3];
        pos->y = p->points[i * 3 + 1];
        pos->z = p->points[i * 3 + 2];
        *pos = transform * Vector4(*pos, 1.0f);
        center = center + *pos;

        // LOG_F(INFO, "Pos = [%f, %f, %f]", XYZ(pos[0]));
    }

    center = center / float(md.o.num_vertices); // Center of mass

    if (create_normals) {
        for (u32 i = 0; i < md.o.num_vertices; ++i) {
            auto normal =
                reinterpret_cast<Vector3 *>(md.buffer + i * md.o.packed_attr_size + md.o.normal_offset);
            normal->x = p->normals[i * 3];
            normal->y = p->normals[i * 3 + 1];
            normal->z = p->normals[i * 3 + 2];
        }
    }

    if (dummy_texcoords) {
        assert(create_normals);
        struct PackedAttrFormat {
            Vector3 pos;
            Vector3 normal;
            Vector2 st;
        };
        // Z positive
        u32 i = 0;
        auto pack = reinterpret_cast<PackedAttrFormat *>(md.buffer + i * md.o.packed_attr_size);
        // Recheck this
        for (u32 j = 0; j < 6; ++j) {
            pack[i++].st = Vector2{ 1.0f, 0.0f };
            pack[i++].st = Vector2{ 1.0f, 1.0f };
            pack[i++].st = Vector2{ 0.0f, 1.0f };
            pack[i++].st = Vector2{ 0.0f, 1.0f };
            pack[i++].st = Vector2{ 0.0f, 0.0f };
            pack[i++].st = Vector2{ 1.0f, 0.0f };
        }
    }

    memcpy((void *)mesh::indices_begin(md), (void *)p->triangles, md.o.num_faces * 3 * sizeof(u16));
}

void load_sphere_mesh(mesh::Model &m, int slices, int stacks, const fo::Matrix4x4 &transform) {
    using namespace math;

    par_shapes_mesh *p = par_shapes_create_parametric_sphere(slices, stacks);

    DEFERSTAT(par_shapes_free_mesh(p));

    // Push a mesh struct
    push_back(m._mesh_array, {});

    struct PackedAttrFormat {
        Vector3 pos;
        Vector3 normal;
        Vector2 st;
    };

    u32 vertex_buffer_size = p->npoints * sizeof(PackedAttrFormat);
    u32 index_buffer_size = p->ntriangles * 3 * sizeof(u16);

    // Init the mesh struct
    auto &md = m._mesh_array[0];
    md.buffer = (uint8_t *)m._buffer_allocator->allocate(vertex_buffer_size + index_buffer_size);

    md.o.packed_attr_size = sizeof(PackedAttrFormat);
    md.o.num_vertices = p->npoints;
    md.o.num_faces = p->ntriangles;

    md.o.position_offset = 0;
    md.o.normal_offset = offsetof(PackedAttrFormat, normal);
    md.o.tex2d_offset = offsetof(PackedAttrFormat, st);
    md.o.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;

    // Fill the vertex data for each vertex
    for (u32 i = 0; i < md.o.num_vertices; ++i) {
        auto attr = reinterpret_cast<PackedAttrFormat *>(md.buffer + i * sizeof(PackedAttrFormat));
        attr->pos.x = p->points[i * 3];
        attr->pos.y = p->points[i * 3 + 1];
        attr->pos.z = p->points[i * 3 + 2];

        attr->pos = transform * Vector4(attr->pos, 1.0f);

        attr->normal.x = p->normals[i * 3];
        attr->normal.y = p->normals[i * 3 + 1];
        attr->normal.z = p->normals[i * 3 + 2];

        attr->normal = transform * Vector4(attr->normal, 0.0f);

        attr->st.x = p->tcoords[i * 2];
        attr->st.y = p->tcoords[i * 2 + 1];
    }

    memcpy((void *)mesh::indices_begin(md), p->triangles, md.o.num_faces * 3 * sizeof(u16));
}

void load_plane_mesh(mesh::Model &m, const fo::Matrix4x4 &transform) {
    struct PackedAttrFormat {
        Vector3 pos;
        Vector3 normal;
        Vector2 st;
    };

    Array<PackedAttrFormat> points(memory_globals::default_allocator());
    resize(points, 4);

    points[0].pos = Vector3{ -0.5f, -0.5f, 0.0f };
    points[0].normal = unit_z;
    points[0].st = Vector2{ 0.0f, 0.0f };

    points[1].pos = Vector3{ 0.5f, -0.5f, 0.0f };
    points[1].normal = unit_z;
    points[1].st = Vector2{ 1.0f, 0.0f };

    points[2].pos = Vector3{ -0.5f, 0.5f, 0.0f };
    points[2].normal = unit_z;
    points[2].st = Vector2{ 0.0f, 1.0f };

    points[3].pos = Vector3{ 0.5f, 0.5f, 0.0f };
    points[3].normal = unit_z;
    points[3].st = Vector2{ 1.0f, 1.0f };

    push_back(m._mesh_array, {});

    auto &md = m._mesh_array[0];

    const u32 vertex_buffer_size = (u32)vec_bytes(points);
    const u32 index_buffer_size = (u32)(6 * sizeof(uint16_t));

    md.buffer = (uint8_t *)m._buffer_allocator->allocate(vertex_buffer_size + index_buffer_size);
    md.o.packed_attr_size = sizeof(PackedAttrFormat);
    md.o.position_offset = 0;
    md.o.normal_offset = offsetof(PackedAttrFormat, normal);
    md.o.tex2d_offset = offsetof(PackedAttrFormat, st);
    md.o.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.num_vertices = 4;
    md.o.num_faces = 2;

    memcpy(md.buffer, data(points), vec_bytes(points));

    uint16_t *indices = (uint16_t *)(md.buffer + vec_bytes(points));
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 1;
    indices[4] = 3;
    indices[5] = 2;
}

void load_screen_quad_mesh(mesh::Model &m) {
    auto &md = push_back_get(m._mesh_array, {});

    Vector2 positions[] = {
        { -1.0f, 1.0f },
        { -1.0f, -1.0f },
        { 1.0f, -1.0f },
        { 1.0f, 1.0f },
    };

    u16 indices[] = { 0, 1, 2, 2, 3, 0 };

    u32 vertex_buffer_size = 4 * sizeof(Vector2);
    u32 index_buffer_size = 6 * sizeof(u16);
    md.buffer = (u8 *)m._buffer_allocator->allocate(vertex_buffer_size + index_buffer_size);
    memcpy(md.buffer, positions, sizeof(positions));
    memcpy(md.buffer + sizeof(positions), indices, sizeof(indices));

    md.positions_are_2d = true;
    md.o.num_vertices = 4;
    md.o.num_faces = 2;
    md.o.position_offset = 0;
    md.o.normal_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.tex2d_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.o.packed_attr_size = sizeof(Vector2);
}

} // namespace eng

namespace eng {

std::aligned_storage_t<sizeof(ShapeMeshes)> mesh_shape_mesh_struct_storage[1];

void init_shape_mesh_struct(ShapeMeshes &shape_meshes) {
    mesh::Model cube;
    mesh::Model sphere;
    mesh::Model plane;
    mesh::Model screen_quad;

    Matrix4x4 xform = uniform_scale_matrix(2.0f);

    load_cube_mesh(cube, xform, true, true);
    load_sphere_mesh(sphere, 5, 5, xform);
    load_plane_mesh(plane, xform);
    load_screen_quad_mesh(screen_quad);

    GLuint vbo;
    GLuint ebo;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    u32 vbo_size = cube[0].o.get_vertices_size_in_bytes() + sphere[0].o.get_vertices_size_in_bytes() +
                   plane[0].o.get_vertices_size_in_bytes() + screen_quad[0].o.get_vertices_size_in_bytes();

    u32 ebo_size = cube[0].o.get_indices_size_in_bytes() + sphere[0].o.get_indices_size_in_bytes() +
                   plane[0].o.get_indices_size_in_bytes() + screen_quad[0].o.get_indices_size_in_bytes();

    glBufferData(GL_ARRAY_BUFFER, vbo_size, nullptr, GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo_size, nullptr, GL_STATIC_DRAW);

    CHECK_F(mesh::have_same_attributes(cube[0].o, sphere[0].o));
    CHECK_F(mesh::have_same_attributes(cube[0].o, plane[0].o));

    // All StrippedMeshData have same attribute offset.
    shape_meshes.mesh_data = mesh::StrippedMeshData(cube[0].o);

    shape_meshes.num_cube_vertices = cube[0].o.num_vertices;
    shape_meshes.num_sphere_vertices = sphere[0].o.num_vertices;
    shape_meshes.num_plane_vertices = plane[0].o.num_vertices;
    shape_meshes.num_screen_quad_vertices = screen_quad[0].o.num_vertices;

    // Fill vertex and index buffer and store the offsets into them for each shape.

    // Fill the vertex buffer
    {

        u8 *vbo_p = (u8 *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

        u32 bytes_written = 0;

        memcpy(vbo_p + bytes_written, cube[0].buffer, cube[0].o.get_vertices_size_in_bytes());
        shape_meshes.cube_vbo_offset = bytes_written;
        bytes_written += cube[0].o.get_vertices_size_in_bytes();

        memcpy(vbo_p + bytes_written, sphere[0].buffer, sphere[0].o.get_vertices_size_in_bytes());
        shape_meshes.sphere_vbo_offset = bytes_written;
        bytes_written += sphere[0].o.get_vertices_size_in_bytes();

        memcpy(vbo_p + bytes_written, plane[0].buffer, plane[0].o.get_vertices_size_in_bytes());
        shape_meshes.plane_vbo_offset = bytes_written;
        bytes_written += plane[0].o.get_vertices_size_in_bytes();

        memcpy(vbo_p + bytes_written, screen_quad[0].buffer, screen_quad[0].o.get_vertices_size_in_bytes());
        shape_meshes.screen_quad_vbo_offset = bytes_written;
        bytes_written += screen_quad[0].o.get_vertices_size_in_bytes();

        glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    // Fill the index buffer
    {
        u8 *ebo_p = (u8 *)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);

        u32 bytes_written = 0;

        memcpy(ebo_p + bytes_written, indices_begin(cube[0]), cube[0].o.get_indices_size_in_bytes());
        shape_meshes.cube_ebo_offset = bytes_written;
        bytes_written += cube[0].o.get_indices_size_in_bytes();

        memcpy(ebo_p + bytes_written, indices_begin(sphere[0]), sphere[0].o.get_indices_size_in_bytes());
        shape_meshes.sphere_ebo_offset = bytes_written;
        bytes_written += sphere[0].o.get_indices_size_in_bytes();

        memcpy(ebo_p + bytes_written, indices_begin(plane[0]), plane[0].o.get_indices_size_in_bytes());
        shape_meshes.plane_ebo_offset = bytes_written;
        bytes_written += plane[0].o.get_indices_size_in_bytes();

        memcpy(ebo_p + bytes_written,
               indices_begin(screen_quad[0]),
               screen_quad[0].o.get_indices_size_in_bytes());
        shape_meshes.screen_quad_ebo_offset = bytes_written;
        bytes_written += screen_quad[0].o.get_indices_size_in_bytes();

        glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    }

    shape_meshes.pos_normal_uv_vao = g_bs().pos_normal_uv_vao;
    shape_meshes.pos2d_vao = g_bs().pos2d_vao;
}

// Loads the app config it points to a non-empty path
TU_LOCAL void load_app_config(inistorage::Storage &config_ini) {
#if !defined(LOGL_APP_CONFIG_PATH)
    return;
#endif

    const auto path = fs::path(LOGL_APP_CONFIG_PATH);
    const auto path_u8string = path.u8string();

    CHECK_F(fs::exists(path), "App config file - %s - doesn't exist", path_u8string.c_str());

    config_ini.init_from_file(path);
}

} // namespace eng

namespace eng {
// -- Renderdoc load, capture, implementation

// Pointer to renderdoc api functions struct
RENDERDOC_API_1_1_2 *g_rdoc;

static bool capture_in_progress;

void dont_load_renderdoc() {
    g_rdoc = nullptr;
    capture_in_progress = false;
}

#if defined(WIN32) || defined(__linux__)
#    define RDOC_AVAILABLE 1
#else
#    define RDOC_AVAILABLE 0
#endif

void load_renderdoc(const char *capture_path_template) {
    fs::path rdoc_dll_path;

    // Prefer the logl_config.ini variable
    {
        std::string rdoc_dll_path_str;
        INI_STORE_DEFAULT(
            "renderdoc_dll_path", eng::gl().config_ini.string, rdoc_dll_path_str, LOGL_RENDERDOC_DLL_PATH);
        rdoc_dll_path = rdoc_dll_path_str;
        LOG_F(INFO, "rdoc_dll_path_str = %s", rdoc_dll_path.c_str());
    }
    auto pathstr = rdoc_dll_path.u8string();

    g_rdoc = nullptr;

#if defined(WIN32)
    auto rdoc_dll = LoadLibrary(pathstr.c_str());

    if (rdoc_dll == (HINSTANCE)NULL) {
        LOG_F(WARNING, "Failed to load renderdoc.dll, won't capture");
        return;
    }

    auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress((HMODULE)rdoc_dll, "RENDERDOC_GetAPI");
    if (RENDERDOC_GetAPI == nullptr) {
        LOG_F(WARNING, "Failed to GetProcAddress RENDERDOC_GetAPI");
    }

#elif defined(__linux__)
    auto rdoc_dll = dlopen(pathstr.c_str(), RTLD_GLOBAL | RTLD_NOW);
    if (rdoc_dll == nullptr) {
        LOG_F(WARNING, "Failed to dlopen librenderdoc.so - %s - %s", pathstr.c_str(), dlerror());
        return;
    }

    auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(rdoc_dll, "RENDERDOC_GetAPI");
    if (RENDERDOC_GetAPI == nullptr) {
        LOG_F(WARNING, "Failed to GetProcAddress RENDERDOC_GetAPI");
    }

#else
#    warning "Unknown platform"
    LOG_F(WARNING, "Renderdoc not loaded. g_rdoc is nullptr");

#endif

#if RDOC_AVAILABLE

    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void **>(&g_rdoc));
    if (ret == 0) {
        LOG_F(WARNING, "Failed to RENDERDOC_GetAPI, won't capture");
        return;
    }

    fs::path capture_path;

    if (capture_path_template == nullptr) {
        capture_path = fs::path(LOGL_SOURCE_DIR) / "captures" / "unspecified";
    } else {
        capture_path = fs::path(capture_path_template);
    }

    auto str = capture_path.u8string();
    g_rdoc->SetCaptureFilePathTemplate(str.c_str());

    auto capture_key = eRENDERDOC_Key_F12;
    g_rdoc->SetCaptureKeys(&capture_key, 1);

    capture_in_progress = false;

    LOG_F(INFO, "Renderdoc loaded. Capture files are at - %s", g_rdoc->GetCaptureFilePathTemplate());

#endif
}

void shutdown_renderdoc() {
#if RDOC_AVAILABLE
    g_rdoc->Shutdown();
#endif
}

void start_renderdoc_frame_capture(GLFWwindow *window) {
    if (!g_rdoc) {
        return;
    }
#if defined(WIN32)
    auto context = glfwGetWGLContext(window);
    auto native_window = glfwGetWin32Window(window);

#elif defined(__linux__)
    auto context = glfwGetGLXContext(window);
    auto native_window = glfwGetGLXWindow(window);

#else
#    warning "Unknown platform"
#endif

#if RDOC_AVAILABLE
    CHECK_EQ_F(capture_in_progress, false, "Capture already in progress.");

    g_rdoc->StartFrameCapture(context, (void *)native_window);
    capture_in_progress = true;
#endif
}

void end_renderdoc_frame_capture(GLFWwindow *window) {
    if (!g_rdoc) {
        return;
    }

#if defined(WIN32)
    auto context = glfwGetWGLContext(window);
    auto native_window = glfwGetWin32Window(window);

#elif defined(__linux__)
    auto context = glfwGetGLXContext(window);
    auto native_window = glfwGetGLXWindow(window);

#else
#    warning "Unknown platform"
#endif

#if RDOC_AVAILABLE
    CHECK_EQ_F(capture_in_progress, true);
    CHECK_EQ_F(g_rdoc->IsFrameCapturing(), 0u, "Renderdoc not capturing any frame.");

    int ret = g_rdoc->EndFrameCapture(context, (void *)native_window);
    if (ret != 1) {
        string_stream::Buffer ss(memory_globals::default_allocator());
        print_callstack(ss);
        CHECK_EQ_F(ret, 1, "EndFrameCapture failed - callstack -\n%s", string_stream::c_str(ss));
    }

    capture_in_progress = false;
#endif
}

bool is_renderdoc_frame_capturing() {
#if RDOC_AVAILABLE
    if (g_rdoc != nullptr) {
        return g_rdoc->IsFrameCapturing();
    }
    return false;
#endif
}

void trigger_renderdoc_frame_capture(u32 num_captures) {
#if RDOC_AVAILABLE
    if (!g_rdoc) {
        return;
    }

    if (num_captures == 1) {
        g_rdoc->TriggerCapture();
    } else {
        g_rdoc->TriggerMultiFrameCapture(num_captures);
    }
#endif
}
} // namespace eng
