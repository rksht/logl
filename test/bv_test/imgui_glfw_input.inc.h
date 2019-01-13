#pragma once

#include <learnogl/essential_headers.h>

#include "imgui_gl3_render.inc.h"

#if defined(WIN32)
#	define GLFW_EXPOSE_NATIVE_WGL
#	define GLFW_EXPOSE_NATIVE_WIN32

#elif defined(__linux__)

#	define GLFW_EXPOSE_NATIVE_GLX
#	define GLFW_EXPOSE_NATIVE_X11

#else
#	warning "Unknown platform"

#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <imgui.h>

struct ImguiGlfwData {
	GLFWwindow *window;
	double last_timestamp;
	std::array<bool, 5> mouse_just_pressed = {};
	std::array<GLFWcursor *, ImGuiMouseCursor_COUNT> mouse_cursors = {};

	// Chain GLFW callbacks: these callbacks will call the user's previously installed callbacks, if any.
	struct {
		GLFWmousebuttonfun mouse_button_fn;
		GLFWscrollfun scroll_fn;
		GLFWkeyfun key_fn;
		GLFWcharfun char_fn;
	} prev_user_cb = {};
};

GLOBAL_STORAGE(ImguiGlfwData, imgui_glfw_data);
GLOBAL_ACCESSOR_FN(ImguiGlfwData, imgui_glfw_data, g_imglfw);

#define G_IMGLFW g_imglfw()

static const char *ImGui_ImplGlfw_GetClipboardText(void *user_data)
{
	return glfwGetClipboardString((GLFWwindow *)user_data);
}

static void imglfw_mouse_button_cb(GLFWwindow *window, int glfw_button, int glfw_action, int glfw_mods)
{
	if (G_IMGLFW.prev_user_cb.mouse_button_fn) {
		invoke(G_IMGLFW.prev_user_cb.mouse_button_fn, window, glfw_button, glfw_action, glfw_mods);
	}

	if (glfw_action == GLFW_PRESS && glfw_button >= 0 && glfw_button < (int)G_IMGLFW.mouse_cursors.size()) {
		G_IMGLFW.mouse_just_pressed[glfw_button] = true;
	}
}

static void imglfw_scroll_cb(GLFWwindow *window, double xoffset, double yoffset)
{
	if (G_IMGLFW.prev_user_cb.scroll_fn) {
		invoke(G_IMGLFW.prev_user_cb.scroll_fn, window, xoffset, yoffset);
	}

	ImGuiIO &io = ImGui::GetIO();
	io.MouseWheelH += (float)xoffset;
	io.MouseWheel += (float)yoffset;
}

static void imglfw_key_cb(GLFWwindow *window, int glfw_key, int glfw_scancode, int glfw_action, int glfw_mods)
{
	if (G_IMGLFW.prev_user_cb.key_fn) {
		invoke(G_IMGLFW.prev_user_cb.key_fn, window, glfw_key, glfw_scancode, glfw_action, glfw_mods);
	}

	ImGuiIO &io = ImGui::GetIO();

	if (glfw_action == GLFW_PRESS) {
		io.KeysDown[glfw_key] = true;
	}

	if (glfw_action == GLFW_RELEASE) {
		io.KeysDown[glfw_key] = false;
	}

	// Modifiers are not reliable across systems
	// @rksht - idk. Just going with it.
	io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
	io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
	io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
	io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];
}

static void imglfw_char_cb(GLFWwindow *window, unsigned codepoint)
{
	if (G_IMGLFW.prev_user_cb.char_fn) {
		invoke(G_IMGLFW.prev_user_cb.char_fn, window, codepoint);
	}

	ImGuiIO &io = ImGui::GetIO();
	if (codepoint > 0 && codepoint < 0x10000) {
		io.AddInputCharacter((unsigned short)codepoint);
	}
}

static void imglfw_update_mouse_pos_and_buttons()
{
	// Update buttons
	ImGuiIO &io = ImGui::GetIO();
	for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++) {
		// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release
		// events that are shorter than 1 frame.
		io.MouseDown[i] = G_IMGLFW.mouse_just_pressed[i] || glfwGetMouseButton(G_IMGLFW.window, i) != 0;
		G_IMGLFW.mouse_just_pressed[i] = false;
	}

	// Update mouse position
	const ImVec2 mouse_pos_backup = io.MousePos;
	io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	const bool focused = glfwGetWindowAttrib(G_IMGLFW.window, GLFW_FOCUSED) != 0;
	if (focused) {
		if (io.WantSetMousePos) {
			glfwSetCursorPos(G_IMGLFW.window, (double)mouse_pos_backup.x, (double)mouse_pos_backup.y);
		} else {
			double mouse_x, mouse_y;
			glfwGetCursorPos(G_IMGLFW.window, &mouse_x, &mouse_y);
			io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
		}
	}
}

static void imglfw_update_mouse_cursor()
{
	ImGuiIO &io = ImGui::GetIO();

	if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) ||
			glfwGetInputMode(G_IMGLFW.window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
		return;
	}

	ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
	if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
		// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
		glfwSetInputMode(G_IMGLFW.window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
	} else {
		// Show OS mouse cursor
		// FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works
		// here.
		glfwSetCursor(G_IMGLFW.window,
									G_IMGLFW.mouse_cursors[imgui_cursor] ? G_IMGLFW.mouse_cursors[imgui_cursor]
																											 : G_IMGLFW.mouse_cursors[ImGuiMouseCursor_Arrow]);
		glfwSetInputMode(G_IMGLFW.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

static void imglfw_set_clipboard_text(void *user_data, const char *text)
{
	glfwSetClipboardString(REINPCAST(GLFWwindow *, user_data), text);
}

static const char *imglfw_get_clipboard_text(void *user_data)
{
	return glfwGetClipboardString(REINPCAST(GLFWwindow *, user_data));
}

bool imglfw_init(GLFWwindow *window, bool install_callbacks = true)
{
	// Construct the global object
	new (&g_imglfw()) ImguiGlfwData{};

	assert(window != nullptr);
	G_IMGLFW.window = window;
	G_IMGLFW.last_timestamp = 0.0;

	ImGuiIO &io = ImGui::GetIO();

	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |=
		ImGuiBackendFlags_HasSetMousePos; // We can honor io.WantSetMousePos requests (optional, rarely used)

	// KeyMap is a user-modifiable array that ImGui uses to get the corresponding user indices for a given imgui
	// index for each key
	io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
	io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
	io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
	io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
	io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
	io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
	io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
	io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
	io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
	io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
	io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
	io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
	io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
	io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
	io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
	io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

	io.SetClipboardTextFn = imglfw_set_clipboard_text;
	io.GetClipboardTextFn = imglfw_get_clipboard_text;
	io.ClipboardUserData = G_IMGLFW.window;

#if defined(WIN32)
	io.ImeWindowHandle = (void *)glfwGetWin32Window(G_IMGLFW.window);
#endif

	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);

	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	// ^ FIXME: GLFW doesn't have this.

	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);

	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	// ^ FIXME: GLFW doesn't have this.

	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	// ^ FIXME: GLFW doesn't have this.

	G_IMGLFW.mouse_cursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

	if (install_callbacks) {
		G_IMGLFW.prev_user_cb.mouse_button_fn = glfwSetMouseButtonCallback(window, imglfw_mouse_button_cb);
		G_IMGLFW.prev_user_cb.scroll_fn = glfwSetScrollCallback(window, imglfw_scroll_cb);
		G_IMGLFW.prev_user_cb.key_fn = glfwSetKeyCallback(window, imglfw_key_cb);
		G_IMGLFW.prev_user_cb.char_fn = glfwSetCharCallback(window, imglfw_char_cb);
	}

	return true;
}

void imglfw_shutdown()
{
	for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++) {
		glfwDestroyCursor(G_IMGLFW.mouse_cursors[cursor_n]);
		G_IMGLFW.mouse_cursors[cursor_n] = nullptr;
	}
}

void imglfw_new_frame()
{
	ImGuiIO &io = ImGui::GetIO();
	IM_ASSERT(io.Fonts->IsBuilt());

	int w, h, display_w, display_h;

	glfwGetWindowSize(G_IMGLFW.window, &w, &h);
	glfwGetFramebufferSize(G_IMGLFW.window, &display_w, &display_h);

	io.DisplaySize = ImVec2(float(w), float(h));
	io.DisplayFramebufferScale = ImVec2(w > 0 ? display_w / float(w) : 0, h > 0 ? display_h / float(h) : 0);

#if 0
	LOG_F(INFO,
				"io.DisplaySize = [%f, %f], io.DisplayFramebufferScale = [%f, %f]",
				XY(io.DisplaySize),
				XY(io.DisplayFramebufferScale));

#endif

	double current_timestamp = glfwGetTime();

	io.DeltaTime =
		float(G_IMGLFW.last_timestamp > 0.0 ? (current_timestamp - G_IMGLFW.last_timestamp) : 1 / 60.0);

	imglfw_update_mouse_pos_and_buttons();
	imglfw_update_mouse_cursor();

	// TODO: Gamepad navigation mapping.
}
