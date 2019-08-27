// A scene to learn and demosntrate deferred lighting, HDR, and ibl. This scene is just a "timer" application
// where there are 6 cubes with textured faces, each being of a digit. I reckon this is a simple but nice
// application to play with.

#include <learnogl/app_loop.h>
#include <learnogl/colors.h>
#include <learnogl/eng>
#include <learnogl/stopwatch.h>
#include <thread>

#include "ibl_intro_utils.h"

enum Slot {
    SECONDS_0 = 0,
    SECONDS_1,
    MINUTES_0,
    MINUTES_1,
    HOURS_0,
    HOURS_1,

    _COUNT
};

// Separate timer state maintainence done here.
struct TimerState {
    std::array<u8, Slot::_COUNT> _number_at_slot;
    stop_watch::State<std::chrono::high_resolution_clock> _sw;
    double _backlog_seconds;
};

void init_timer(TimerState &c) { std::fill(c._number_at_slot.begin(), c._number_at_slot.end(), 0u); }

void start_timer(TimerState &c) {
    stop_watch::start(c._sw);
    c._backlog_seconds = 0;
}

void _increment_second(TimerState &c) {
    u8 seconds = c._number_at_slot[SECONDS_1] * 10 + c._number_at_slot[SECONDS_0];
    seconds = (seconds + 1) % 60;

    c._number_at_slot[SECONDS_0] = seconds % 10;
    c._number_at_slot[SECONDS_1] = seconds / 10;

    if (seconds == 0) {
        u8 minutes = c._number_at_slot[MINUTES_1] * 10 + c._number_at_slot[MINUTES_0];
        minutes = (minutes + 1) % 60;

        c._number_at_slot[MINUTES_0] = minutes % 10;
        c._number_at_slot[MINUTES_1] = minutes / 10;

        if (minutes == 0) {
            u8 hours = c._number_at_slot[HOURS_1] * 10 + c._number_at_slot[HOURS_0];
            hours = (minutes + 1) % 60;

            c._number_at_slot[HOURS_0] = hours % 10;
            c._number_at_slot[HOURS_1] = hours / 10;
        }
    }
}

// Update the timer, returns true if some number at some slot changed. Granularity is one second.
bool tick_timer(TimerState &c) {
    auto tick_dura = stop_watch::restart(c._sw);
    c._backlog_seconds += seconds(tick_dura);

    u32 times_updated = 0;
    while (c._backlog_seconds >= 1.0) {
        _increment_second(c);
        ++times_updated;
        c._backlog_seconds -= 1.0;
    }

    if (times_updated > 1) {
        LOG_F(WARNING, "Had to update more than once in a single tick");
    }

    return times_updated != 0;
}

eng::StartGLParams glparams;

// Main application class. Deals with rendering.

struct App {
    eng::GLApp *gl;
    TimerState timer_state;
    CubeGlobal cube_global;

    Camera camera;

    struct {
        GLuint special_cube;
    } vao_catalog;

    bool escape_pressed = false;
};

constexpr auto CLEAR_COLOR = colors::Azure;

namespace app_loop {

template <> void init<App>(App &A) {
    init_cube_global(A.cube_global);

    A.camera.set_position(zero_3);
    A.camera.look_at(unit_z);

    A.vao_catalog.special_cube = A.gl->bs.get_vao(CubeGlobal::get_vao_format());

    glClearColor(XYZW(CLEAR_COLOR));
}

template <> void update<App>(App &A, State &state) {
    glfwPollEvents();

    if (glfwGetKey(A.gl->window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        A.escape_pressed = true;
    }
}

template <> void render<App>(App &A) {
    // Proj View transform ubuffer update
    glInvalidateBufferData(A.gl->bs.per_camera_ubo());
    glBindBuffer(GL_UNIFORM_BUFFER, A.gl->bs.per_camera_ubo());
    PerCameraUBlockFormat per_cam;
    eng::update_camera_block(A.camera, per_cam);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindVertexBuffer(0, A.cube_global.vbo, 0, sizeof(Vector3) + sizeof(Vector2) + sizeof(Vector3));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.cube_global.ebo);
    glBindVertexArray(A.vao_catalog.special_cube);

    glDrawElements(GL_TRIANGLES, A.cube_global.num_indices, GL_UNSIGNED_SHORT, 0);

    glfwSwapBuffers(A.gl->window);
}

template <> void close<App>(App &A) {}

template <> bool should_close<App>(App &A) { return glfwWindowShouldClose(A.gl->window) || A.escape_pressed; }

} // namespace app_loop

int main() {
    memory_globals::init();
    DEFERSTAT(memory_globals::shutdown());

    glparams.window_width = 1360;
    glparams.window_height = 768;
    glparams.window_title = "IBL intro";

    eng::GLApp gl;
    eng::start_gl(glparams, gl);
    DEFERSTAT(eng::close_gl(glparams, gl));

    App app;
    app.gl = &gl;

    app_loop::State s {};
    app_loop::run(app, s);
}
