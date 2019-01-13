// Implements communication with ImGui using GLFW, as shown in their examples.

#pragma once

#include <learnogl/essential_headers.h>

#include <imgui.h>
#include <learnogl/input_handler.h>
#include <learnogl/typed_gl_resources.h>
#include <scaffold/bitflags.h>

struct GLFWwindow;

namespace eng {

namespace imgui_glfw {

enum class InputCallbackBits : u32 {
    KEY = 1 << 0,
    CHAR = 1 << 1,
    MOUSE_SCROLL = 1 << 2,
    MOUSE_BUTTON = 1 << 3,
    MOUSE_MOVE = 1 << 4,
};

using InputCallbacksFlags = fo::BitFlags<InputCallbackBits>;

constexpr InputCallbacksFlags all_callbacks = InputCallbackBits::KEY | InputCallbackBits::CHAR |
                                              InputCallbackBits::MOUSE_SCROLL |
                                              InputCallbackBits::MOUSE_BUTTON | InputCallbackBits::MOUSE_MOVE;

struct ImGui_Context_Impl; // Forward declaration for muh pimpl

// This is the main object. Just wraps an implementation struct pointer.
struct ImGui_Context : NonCopyable {
    ImGui_Context_Impl *_pimpl = nullptr;
};

void init(ImGui_Context &imgui_context,
          GLFWwindow *window = gl().window,
          InputCallbacksFlags callbacks = all_callbacks,
          fo::Allocator &allocator = fo::memory_globals::default_allocator(),
          fo::Allocator &callback_allocator = fo::memory_globals::default_allocator());

// Call after polling input events
void after_poll_events(ImGui_Context &imgui_context);

// Call when you want to render the gui. Usually at the end of all scene related rendering.
inline void render(ImGui_Context &imgui_context) { ImGui::Render(); }

// Disable inputs from these callbacks
void disable_input(ImGui_Context &imgui_context, InputCallbacksFlags which_inputs);

// Enable inputs from these callbacks
void enable_input(ImGui_Context &imgui_context, InputCallbacksFlags which_inputs);

// Not relying on the destructor. Call this when you want to teardown the context.
void shutdown(ImGui_Context &imgui_context);

// Sets the default callback functions.
void set_default_callbacks();

void default_on_key_cb(GLFWwindow *window, int key, int scancode, int action, int mods);
void default_on_char_cb(GLFWwindow *window, unsigned int codepoint);
void default_on_mouse_scroll_cb(GLFWwindow *window, double xoffset, double yoffset);
void default_on_mouse_button_cb(GLFWwindow *window, int button, int action, int mods);
void default_on_mouse_move_cb(GLFWwindow *window, double xpos, double ypos);

using KeyCallbackFn = decltype(default_on_key_cb);
using CharCallbackFn = decltype(default_on_char_cb);
using MouseScrollCallbackFn = decltype(default_on_mouse_scroll_cb);
using MouseButtonCallbackFn = decltype(default_on_mouse_button_cb);
using MouseMoveCallbackFn = decltype(default_on_mouse_move_cb);

// Giving nullptr will disable inputs from being received by imgui for that type of event
void set_key_callback(ImGui_Context *c, KeyCallbackFn cb);
void set_char_callback(ImGui_Context *c, CharCallbackFn cb);
void set_mouse_scroll_callback(ImGui_Context *c, MouseScrollCallbackFn  cb);
void set_mouse_button_callback(ImGui_Context *c, MouseButtonCallbackFn cb);
void set_mouse_move_callback(ImGui_Context *c, MouseMoveCallbackFn cb);

} // namespace imgui_glfw

} // namespace eng
