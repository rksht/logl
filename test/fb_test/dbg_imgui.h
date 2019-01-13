// Implements communication with ImGui using GLFW, as shown in their examples.

#pragma once

#include <glad/glad.h>

#include <GLFW/glfw3.h>
#include <imgui.h>

namespace imgui {

struct InstallCallbacks {
    bool key_cb;
    bool char_cb;
    bool mouse_scroll_cb;
    bool mouse_button_cb;
    bool mouse_move_cb;
};

constexpr InstallCallbacks default_install_cb = {true, true, true, true, true};

// Use fo::memory_globals::default_allocator?
constexpr bool use_tracking_allocator = true;

// We initialize, update and shutdown imgui using these two calls
void init_imgui(GLFWwindow *window, InstallCallbacks install_callbacks = default_install_cb);
void update_imgui();
inline void render_imgui() { ImGui::Render(); }
void shutdown_imgui();

// _Mod_ - Input processing callbacks. Define/adjust these in the .cpp file.
// We could install these callbacks into glfw and first communicate the input
// to imgui and then to the application via these, OR we could register our
// own callbacks and keep imgui related callback-processing in these functions
// and invoke these from our actually installed callbacks if we need to. The
// second option sounds better to me in general app, and in this case we
// should set `install_callbacks` to false.
void on_key(GLFWwindow *window, int key, int scancode, int action, int mods);
void on_char(GLFWwindow *window, unsigned int codepoint);
void on_mouse_scroll(GLFWwindow *window, double xoffset, double yoffset);
void on_mouse_scroll(GLFWwindow *window, double xoffset, double yoffset);
void on_mouse_button(GLFWwindow *window, int button, int action, int mods);
void on_mouse_move(GLFWwindow *window, double xpos, double ypos);

} // namespace imgui
