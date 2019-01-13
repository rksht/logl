#pragma once

#include <GLFW/glfw3.h>

#include <scaffold/memory.h>

#include <memory>
#include <type_traits>

namespace eng {

// The "low-level" input system.
namespace input {

struct OnKeyArgs {
    int key;
    int scancode;
    int action;
    int mods;
    GLFWwindow *window;
};

struct OnMouseMoveArgs {
    double xpos;
    double ypos;
    GLFWwindow *window;
};

struct OnMouseButtonArgs {
    int button;
    int action;
    int mods;
    GLFWwindow *window;
};

struct OnCharInputArgs {
    unsigned int codepoint;
    GLFWwindow *window;
};

// Abstract class specifying the functions that a specific input processor will implement. All instances are
// used via std::unique_ptr.
template <typename App> class InputHandlerBase {
  public:
    using AppType = App;

    virtual ~InputHandlerBase() {}

    // The default implementations of these are no-ops. Override these in your derived implementation.

    virtual void handle_on_key(App &, OnKeyArgs) {}

    virtual void handle_on_mouse_move(App &, OnMouseMoveArgs) {}

    virtual void handle_on_mouse_button(App &, OnMouseButtonArgs) {}

    virtual void handle_on_char_input(App &, OnCharInputArgs) {}
};

namespace eng_internal {

template <typename App> void delete_handler(InputHandlerBase<App> *input_object) {
    fo::make_delete(fo::memory_globals::default_scratch_allocator(), input_object);
}

} // namespace eng_internal

template <typename App>
using BaseHandlerPtr = std::unique_ptr<InputHandlerBase<App>, decltype(&eng_internal::delete_handler<App>)>;

// Always create new input handler objects using this function.
template <typename HandlerDerivedClass, typename... Args> auto make_handler(Args &&... handler_ctor_args) {
    using App = typename HandlerDerivedClass::AppType;

    static_assert(std::is_base_of<InputHandlerBase<App>, HandlerDerivedClass>::value, "");

    // clang-format off

    return BaseHandlerPtr<App>
    (
        fo::make_new<HandlerDerivedClass>(
            fo::memory_globals::default_scratch_allocator(),
            std::forward<Args>(handler_ctor_args)...
        ),
        &eng_internal::delete_handler<App>
    );
    // clang-format on
}

template <typename App> auto make_default_handler() {
    return BaseHandlerPtr<App>(
        fo::make_new<InputHandlerBase<App>>(fo::memory_globals::default_scratch_allocator()),
        &eng_internal::delete_handler<App>);
}

} // namespace input

namespace eng_internal {

template <typename App> struct GLFWCallbacks {
    static void on_key(GLFWwindow *window, int key, int scancode, int action, int mods) {
        auto app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
        app->current_input_handler()->handle_on_key(*app,
                                                    input::OnKeyArgs{key, scancode, action, mods, window});
    }

    static void on_mouse_move(GLFWwindow *window, double xpos, double ypos) {
        auto app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
        app->current_input_handler()->handle_on_mouse_move(*app, input::OnMouseMoveArgs{xpos, ypos, window});
    }

    static void on_mouse_button(GLFWwindow *window, int button, int action, int mods) {
        auto app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
        app->current_input_handler()->handle_on_mouse_button(
            *app, input::OnMouseButtonArgs{button, action, mods, window});
    }

    static void on_char_input(GLFWwindow *window, unsigned int codepoint) {
        auto app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
        app->current_input_handler()->handle_on_char_input(*app, input::OnCharInputArgs{codepoint, window});
    }
};

template <typename App> inline void register_glfw_callbacks(App &app, GLFWwindow *window) {
    using CallbacksClass = GLFWCallbacks<App>;

    glfwSetWindowUserPointer(window, &app);
    glfwSetKeyCallback(window, CallbacksClass::on_key);
    glfwSetCursorPosCallback(window, CallbacksClass::on_mouse_move);
    glfwSetMouseButtonCallback(window, CallbacksClass::on_mouse_button);
    glfwSetCharCallback(window, CallbacksClass::on_char_input);
}

} // namespace eng_internal

#define DEFINE_GLFW_CALLBACKS(AppClassName) template struct eng_internal::GLFWCallbacks<AppClassName>

#define REGISTER_GLFW_CALLBACKS(ref_to_app, window_ptr)                                                      \
    eng_internal::register_glfw_callbacks(ref_to_app, window_ptr)

} // namespace eng
