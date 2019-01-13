// The sorting test from fluidcs11, just in case I need to test it later.

#include <learnogl/app_loop.h>
#include <learnogl/gl_misc.h>

#include <learnogl/bitonic_sort.h>

using namespace fo;
using namespace math;

using namespace std::string_literals;

// Particle state
struct Particle {
    Vector2 position;
    Vector2 velocity;
};

struct ParticleDensity {
    f32 density;
};

struct ParticleForce {
    Vector2 acceleration;
};

constexpr u32 NUM_GRID_CELLS = 65536;
constexpr u32 SIMULATION_BLOCK_SIZE = 256;

// I restrict the max threads per group to D3D11 hardware capabilities
constexpr std::array<u32, 3> MAX_THREADS_PER_GROUP = { 1024, 1024, 64 };

// Number of threads per block for the bitonic sorting
constexpr u32 NUM_ELEMENTS_PER_GROUP = 512;
constexpr u32 TRANSPOSE_BLOCK_SIZE = 16;
constexpr u32 MAX_ELEMENTS_ALLOWED = NUM_ELEMENTS_PER_GROUP * NUM_ELEMENTS_PER_GROUP;

constexpr u32 NUM_ELEMENTS_HALFK = 512;
constexpr u32 NUM_PARTICLES_8K = 8 * 1024;
constexpr u32 NUM_PARTICLES_16K = 16 * 1024;
constexpr u32 NUM_PARTICLES_32K = 32 * 1024;
constexpr u32 NUM_PARTICLES_64K = 64 * 1024;

constexpr f32 GRAVITY_ACC = 9.8f;

constexpr u32 SRC_INDEX = 0;
constexpr u32 DEST_INDEX = 1;

inline constexpr Vector2 gravity_down(f32, f32);
inline constexpr Vector2 gravity_up(f32, f32);
inline constexpr Vector2 gravity_left(f32, f32);
inline constexpr Vector2 gravity_right(f32, f32);

enum class SimulationMode : u32 { SIMPLE, SHARED, GRID };

enum WallName { BOTTOM, RIGHT, TOP, LEFT };

inline constexpr u32 num_elements_in_shared_mem(u32 element_size) {
    constexpr u32 MIN_SHARED_MEM_SIZE = 32u << 10;
    return MIN_SHARED_MEM_SIZE / element_size;
}

struct Config {
    // These will control how the fluid behaves
    f32 initial_particle_spacing = 0.0045f;
    f32 smooth_len = 0.012f;
    f32 pressure_stiffness = 200.0f;
    f32 rest_density = 1000.0f;
    f32 particle_mass = 0.0002f;
    f32 viscosity = 0.1f;
    f32 max_allowable_timestep = 0.005f;
    f32 particle_render_size = 0.003f;

    // Gravity directions
    Vector2 gravity_up;

    f32 map_height = 1.2f;
    f32 map_width = (4.0f / 3.0f) * map_height;

    // Map wall collision planes (or rather, lines or edges) in the (N, D) form
    f32 wall_stiffness = 3000.0f;
    std::array<Vector3, 4> planes = {
        Vector3 { 1.f, 0.f, 0.f },        // LEFT
        Vector3 { 0.f, 1.f, 0.f },        // BOTTOM
        Vector3 { -1.f, 0.f, map_width }, // RIGHT
        Vector3 { 0.f, -1.f, map_height } // TOP
    };
};

Config gconf;

// Trying out this segregattion of resources as done in the DirectX sample

struct GLGlobalResources {
    GLuint null_pbo = 0;

    GLuint particle_vs = 0;
    GLuint particle_gs = 0;
    GLuint particle_fs = 0;
    GLuint particle_prog = 0;

    GLuint build_grid_cs = 0;
    GLuint clear_grid_indices_cs = 0;
    GLuint build_grid_indices_cs = 0;
    GLuint rearrange_particles_cs = 0;

    // Depending on the algo, we will use the following kernels for density and force
    GLuint density_simple_cs = 0;
    GLuint force_simple_cs = 0;
    GLuint density_shared_cs = 0;
    GLuint force_shared_cs = 0;
    GLuint density_grid_cs = 0;
    GLuint force_grid_cs = 0;

    GLuint integrate_cs = 0;

    GLuint sort_bitonic_cs = 0;
    GLuint sort_bitonic_prog = 0;
    GLuint sort_transpose_cs = 0;
    GLuint sort_transpose_prog = 0;

    BufferDeleter particles_ssbo;
    BufferDeleter sorted_particles_ssbo;

    BufferDeleter particle_density_ssbo;
    BufferDeleter particle_forces_ssbo;
    BufferDeleter grid_ssbo;

    BufferDeleter grid_source_pong_ssbo;
    BufferDeleter grid_indices_ssbo;

    std::array<BufferDeleter, 2> numbers_ssbos;
};

struct UniformBuffer {
    u32 num_particles;
    f32 timestep;
    f32 pressure_stiffness;
    f32 rest_density;
    f32 density_coef;
    f32 grad_pressure_coef;
    f32 wall_stiffness;

    alignas(8) Vector2 gravity;
    Vector4 grid_dim;
    Vector4 planes[4];
};

eng::StartGLParams glparams;

void init_gl_params() {
    glparams.window_title = "SPH in OpenGL";
    glparams.window_width = 1024;
    glparams.window_height = 768;
    glparams.load_renderdoc = false;
}

struct App {
    eng::GLApp *gl = nullptr;

    GLGlobalResources res;

    bool escape_pressed = false;
};

void bitonic_sort_test(App &app) {
    // Config
    u32 NUM_ELEMENTS = NUM_ELEMENTS_PER_GROUP * TRANSPOSE_BLOCK_SIZE;
    LOG_F(INFO,
          "Sorting %u groups worth of elements, i.e. %u elements ",
          NUM_ELEMENTS / NUM_ELEMENTS_PER_GROUP,
          NUM_ELEMENTS);

    // Create an array of numbers at random..ish
    std::vector<u32> host_side_array(NUM_ELEMENTS);
    for (u32 i = 0; i < NUM_ELEMENTS; ++i) {
        host_side_array[i] = (NUM_ELEMENTS - i - 1) * (u32)rng::random(1, 1000);
    }

    // Sort them on cpu side for checking if the gpu side implementation returns a correct result or not
    std::vector<u32> host_side_sorted(host_side_array.begin(), host_side_array.end());
    std::sort(host_side_sorted.begin(), host_side_sorted.end());

    // Create the sorting buffers
    BitonicSorterU32 sorter;
    sorter.load_shader(eng::gl().bs);

    CHECK_F(sorter.set_elements(host_side_array.data(), (u32)host_side_array.size()));

    const auto ts_before_sort = std::chrono::high_resolution_clock::now();
    sorter.compute_sort();

    const auto ts_after_sort = std::chrono::high_resolution_clock::now();
    double total_time = seconds(ts_after_sort - ts_before_sort);

    LOG_F(INFO,
          "Done sorting all %u elements, time taken = %.6f ms",
          NUM_ELEMENTS,
          total_time * 1e-3);

    u32 *ssbo_mem = sorter.map_readable();

    // Compare the sorting results
    const bool equal = std::equal(ssbo_mem, ssbo_mem + NUM_ELEMENTS, host_side_sorted.data());
    sorter.unmap();

    if (!equal) {
        LOG_F(ERROR, "%s Not equal", SCREAM);
        exit(EXIT_FAILURE);
    } else {
        LOG_F(INFO, "Equal!!");
    }
}

namespace app_loop {

template <> void init<App>(App &app) {
    bitonic_sort_test(app);
    // app.escape_pressed = true;
}

template <> void close<App>(App &app) {}

template <> void update(App &app, State &loop_timer) { glfwPollEvents(); }

template <> void render(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glfwSwapBuffers(app.gl->window);
}

template <> bool should_close(App &app) {
    return app.escape_pressed || glfwWindowShouldClose(app.gl->window);
}

} // namespace app_loop

int main(int ac, char **av) {
    memory_globals::init();
    DEFERSTAT(memory_globals::shutdown());

    init_gl_params();

    eng::start_gl(glparams, eng::gl());

    DEFERSTAT(eng::close_gl(glparams, eng::gl()));

    {
        App app;
        app.gl = &eng::gl();

        app_loop::State timer = {};
        app_loop::run(app, timer);
    }
}
