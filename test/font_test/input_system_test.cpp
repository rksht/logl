#include "essentials.h"

#include <chaiscript/chaiscript.hpp>
#include <learnogl/chaiscript_console_cb.h>
#include <learnogl/console.h>
#include <learnogl/input_handler.h>

using namespace fo;
using namespace math;
namespace chai = chaiscript;

constexpr i32 window_width = 1024;
constexpr i32 window_height = 640;

void clear_color(double r, double g, double b) { glClearColor((float)r, (float)g, (float)b, 1.0); }

// Main app
class App : NonCopyable {
  public:
    input::BaseHandlerPtr<App> input_handler = input::make_handler<input::InputHandlerBase<App>>();
    GLFWwindow *window;

    bool in_console_handler = false;

    console::Console _console;

    chai::ChaiScript _chai;

  public:
    App() { _chai.add(chai::fun(&clear_color), "clear_color"); }

    auto &current_input_handler() { return input_handler; }
    void set_input_handler(input::BaseHandlerPtr<App> handler_ptr) { input_handler = std::move(handler_ptr); }
};

class KeyPressHandler;

class MouseQuadrantHandler : public input::InputHandlerBase<App> {
    App &_app;
    AABB _quadrants[4];
    unsigned _prev_quadrant_number = 4;

  public:
    MouseQuadrantHandler(App &app)
        : _app(app) {
        _quadrants[0] = AABB{{0.0f, 0.0f, 0.0f}, {window_width / 2.0f, window_height / 2.0f, 0.0f}};
        _quadrants[1] = AABB{Vector3{window_width / 2.0f, window_height / 2.0f, 0.0f},
                             Vector3{(f32)window_width, (f32)window_height, 0.0f}};
        _quadrants[2] = AABB{Vector3{window_width / 2.0f, 0.0f, 0.0f},
                             Vector3{(f32)window_width, window_height / 2.0f, 0.0f}};
        _quadrants[3] = AABB{Vector3{0.0f, (f32)window_height / 2.0f, 0.0f},
                             Vector3{window_width / 2.0f, (f32)window_height, 0.0f}};
    }

    virtual void handle_on_mouse_move(App &app, input::OnMouseMoveArgs args) override;
};

class KeyPressHandler : public input::InputHandlerBase<App> {
    App &_app;

  public:
    KeyPressHandler(App &app)
        : _app(app) {}

    virtual void handle_on_key(App &app, input::OnKeyArgs on_key_args) override;
};

void MouseQuadrantHandler::handle_on_mouse_move(App &app, input::OnMouseMoveArgs args) {
    unsigned i = 0;
    for (; i < 4; ++i) {
        auto &bb = _quadrants[i];
        if (bb.min.x <= args.xpos && args.xpos < bb.max.x && bb.min.y <= args.ypos && args.ypos <= bb.max.y) {
            break;
        }
    }

    if (i != _prev_quadrant_number && _prev_quadrant_number != 4) {
        LOG_F(INFO, "Changing to Key Handler");
        _app.input_handler = input::make_handler<KeyPressHandler>(_app);
        return;
    }

    LOG_F(INFO, "Mouse in same quadrant");

    _prev_quadrant_number = i;
}

void KeyPressHandler::handle_on_key(App &app, input::OnKeyArgs args) {
    switch (args.key) {
    case GLFW_KEY_S: {
        if (args.action == GLFW_PRESS) {
            LOG_F(INFO, "Changing to MouseQuadrantHandler");
            _app.input_handler = input::make_handler<MouseQuadrantHandler>(_app);
            return;
        }
        break;
    }
    case GLFW_KEY_C: {
        if (args.action == GLFW_PRESS) {
            LOG_F(INFO, "Changing to console state");

            auto make_next_handler = [&app]() {
                app.in_console_handler = false;
                LOG_F(INFO, "Changing from console to input handler");
                return input::make_handler<KeyPressHandler>(app);
            };

            _app.in_console_handler = true;

            _app.input_handler = input::make_handler<
                ConsoleCommandHandler<App, ChaiScriptConsoleCallback, decltype(make_next_handler)>>(
                app._console, ChaiScriptConsoleCallback(app._chai), std::move(make_next_handler));

            return;
        }
    } break;
    }

    if (args.action == GLFW_PRESS) {
        LOG_F(INFO, "Staying in KeyPressHandler");
    }
}

DEFINE_GLFW_CALLBACKS(App);

int main() {
    eng::init_global_systems();

    DEFER( []() { eng::close_global_systems(); });

    App app;

    eng::start_gl(&app.window, window_width, window_height, "input_test");

    app.input_handler = input::make_handler<KeyPressHandler>(app);

    console::init(app._console, window_width, window_height, 0.4f, 15.0f, 0, 0);
    console::add_fmt_string_to_pager(app._console, "Hello world");

    REGISTER_GLFW_CALLBACKS(&app, app.window)

    glClearColor(0.8f, 0.8f, 0.88f, 1.0f);

    while (!glfwWindowShouldClose(app.window)) {
        glfwPollEvents();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (app.in_console_handler) {
            draw_prompt(app._console);
            draw_pager_updated(app._console);
            blit_pager(app._console);
        }

        glfwSwapBuffers(app.window);
    }

    glfwTerminate();
}
