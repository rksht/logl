#if 0

#include <learnogl/gl_misc.h>

#include <AntTweakBar.h>

struct TwGlfwHelper {
	GLFWwindow *window;
};

inline void tw_helper_key_cb(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	TwEventKeyGLFW(key, action, mods);
}

inline void tw_helper_char_cb(GLFWwindow *window, unsigned int codepoint)
{
	// TwEventCharGLFW(codepoint, GLFW_PRESS);
}

inline void tw_helper_mouse_button_cb(GLFWwindow *window, int button, int action, int mods)
{
	TwEventMouseButtonGLFW(button, action, mods);
}

inline void tw_helper_mouse_move_cb(GLFWwindow *window, double xpos, double ypos)
{
	TwEventMousePosGLFW(xpos, ypos);
}

void tw_helper_set_all_callbacks(GLFWwindow *window)
{
	glfwSetMouseButtonCallback(window, tw_helper_mouse_button_cb);
	glfwSetKeyCallback(window, tw_helper_key_cb);
	glfwSetCursorPosCallback(window, tw_helper_mouse_move_cb);
}

#endif