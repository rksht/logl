#include "essentials.h"
#include <learnogl/colors.h>
#include <learnogl/stopwatch.h>

using namespace fo;
using namespace math;

int main() {
    eng::init_non_gl_globals();
    DEFERSTAT(eng::close_non_gl_globals());

    eng::StartGLParams start_gl_params;
    start_gl_params.window_width = 1024;
    start_gl_params.window_height = 768;

    GLFWwindow *window = eng::start_gl(start_gl_params);
    DEFERSTAT(eng::close_gl());

    eng::init_gl_globals();
    DEFERSTAT(eng::close_gl_globals());

    glClearColor(XYZW(colors::AntiqueWhite));

    DebugInfoOverlay debug_info;

    debug_info.init(start_gl_params.window_width,
                    start_gl_params.window_height,
                    Vector3(colors::DeepSkyBlue),
                    Vector3(colors::Lavender));

    debug_info.write_string("How deep is your love?");

    double secs_per_frame = 0.0;

    stop_watch::State<std::chrono::high_resolution_clock> sw{};
    stop_watch::start(sw);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double secs = seconds(stop_watch::restart(sw));
        secs_per_frame = 0.8 * secs_per_frame + 0.2 * secs;

        double fps = 1.0 / secs_per_frame;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        debug_info.write_string(fmt::format("fps = {}", std::floor(fps + 0.5)).c_str());

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        debug_info.draw();

        glfwSwapBuffers(window);
    }
}
