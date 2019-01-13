#include "dbg_imgui.h"

#include <glad/glad.h>
#include <learnogl/gl_misc.h>
#include <learnogl/math_ops.h>
#include <scaffold/debug.h>
#include <scaffold/memory.h>

#include <stddef.h> // offsetof

struct ImguiState {
    GLFWwindow *window = NULL;
    bool mouse_pressed[3] = {false, false, false};
    float mouse_wheel = 0.0f;
    GLuint font_texture = 0;
    GLuint shader_program = 0;
    GLuint sampler_loc = 0;
    GLuint proj_mat_loc = 0;
    GLuint position_loc = 0;
    GLuint st_loc = 0;
    GLuint color_loc = 0;
    GLuint vbo = 0;
    GLuint vao = 0;
    GLuint ebo = 0;
} g_state; // The state we keep to communicate with ImGui

// -- Functions operating on this state

// Draws the gui with ogl
static void render_drawlists(ImDrawData *draw_data);
// Initializes gl buffers and shader to be used for ImGui widgets
static void create_device_objects();
// Cleans gl buffers
static void invalidate_device_objects();
// Creates default font atlas texture
static void create_fonts_texture();
// Memory allocator and free function to give ImGui
static void *alloc_fn(size_t sz);
static void free_fn(void *ptr);
// Functions to give to imgui so that it can get and set the system's
// clipboard text.
static const char *get_clipboard_text(void *user_data);
static void set_clipboard_text(void *user_data, const char *text);

namespace imgui {

// _Mod_ - Implementation of callbacks. As described in .h, we can call these
// directly or call these from other callbacks I registered.

void on_key(GLFWwindow *window, int key, int scancode, int action, int mods) {
    ImGuiIO &io = ImGui::GetIO();
    if (action == GLFW_PRESS) {
        io.KeysDown[key] = true;
    }
    if (action == GLFW_RELEASE) {
        io.KeysDown[key] = false;
    }
    (void)scancode;
    io.KeyCtrl = (mods | GLFW_MOD_CONTROL) != 0;
    io.KeyShift = (mods | GLFW_MOD_SHIFT) != 0;
    io.KeyAlt = (mods | GLFW_MOD_ALT) != 0;
    io.KeySuper = (mods | GLFW_MOD_SUPER) != 0;
}

void on_char(GLFWwindow *window, unsigned int codepoint) {
    ImGuiIO &io = ImGui::GetIO();
    if (codepoint > 0 && codepoint < 0x10000) {
        io.AddInputCharacter((unsigned short)codepoint);
    }
}

void on_mouse_scroll(GLFWwindow *window, double xoffset, double yoffset) {
    (void)window;
    (void)xoffset;
    g_state.mouse_wheel += float(yoffset);
}

void on_mouse_button(GLFWwindow *window, int button, int action, int mods) {
    (void)window;
    (void)mods;
    if (action == GLFW_PRESS && button >= 0 && button < 3) {
        g_state.mouse_pressed[button] = true;
    }
}

void on_mouse_move(GLFWwindow *window, double xpos, double ypos) {
    // Define if you need to. Don't forget to install the callback in
    // init_imgui if you are entering the callback chain via imgui.
}

} // namespace imgui

namespace imgui {

// Implementation of init and shutdown

void init_imgui(GLFWwindow *window, InstallCallbacks install_callbacks) {
    g_state.window = window;

    // Keyboard mapping. This is an indirection that ImGui uses to index its
    // KeysDown[].
    ImGuiIO &io = ImGui::GetIO();
    io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
    io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
    io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
    io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

    io.RenderDrawListsFn = render_drawlists;

    io.SetClipboardTextFn = set_clipboard_text;
    io.GetClipboardTextFn = get_clipboard_text;
    io.ClipboardUserData = g_state.window;

    // Install glfw callbacks as requested
    if (install_callbacks.key_cb) {
        glfwSetKeyCallback(window, on_key);
    }

    if (install_callbacks.char_cb) {
        glfwSetCharCallback(window, on_char);
    }

    if (install_callbacks.mouse_scroll_cb) {
        glfwSetScrollCallback(window, on_mouse_scroll);
    }

    if (install_callbacks.mouse_move_cb) {
        glfwSetCursorPosCallback(window, on_mouse_move);
    }

    if (install_callbacks.mouse_button_cb) {
        glfwSetMouseButtonCallback(window, on_mouse_button);
    }

    if (use_tracking_allocator) {
        io.MemAllocFn = alloc_fn;
        io.MemFreeFn = free_fn;
    }
}

void update_imgui() {
    if (!g_state.font_texture) {
        create_device_objects();
    }

    ImGuiIO &io = ImGui::GetIO();

    int w, h;
    int display_w, display_h;
    glfwGetWindowSize(g_state.window, &w, &h);
    glfwGetFramebufferSize(g_state.window, &display_w, &display_h);
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale =
        ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

    if (glfwGetWindowAttrib(g_state.window, GLFW_FOCUSED)) {
        double mouse_x, mouse_y;
        glfwGetCursorPos(g_state.window, &mouse_x, &mouse_y);
        io.MousePos.x = float(mouse_x);
        io.MousePos.y = float(mouse_y);
    } else {
        io.MousePos.x = -1.f;
        io.MousePos.y = -1.f;
    }

    for (int i = 0; i < 3; ++i) {
        io.MouseDown[i] = g_state.mouse_pressed[i] || (glfwGetMouseButton(g_state.window, i) != 0);
        g_state.mouse_pressed[i] = false;
    }

    io.MouseWheel = g_state.mouse_wheel;
    g_state.mouse_wheel = 0.0f;

    // Hide OS mouse cursor if ImGui is drawing it
    glfwSetInputMode(g_state.window, GLFW_CURSOR,
                     io.MouseDrawCursor ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);

    ImGui::NewFrame();
}

void shutdown_imgui() {
    invalidate_device_objects();
    ImGui::Shutdown();
}

} // namespace imgui

// -- Implementation

void create_device_objects() {
    GLint last_texture, last_array_buffer, last_vertex_array;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

    char vert_shader[] = R"(
    #version 330

    uniform mat4 proj_mat;

    in vec2 position;
    in vec2 st;
    in vec4 color;

    out vec2 frag_st;
    out vec4 frag_color;

    void main() {
        frag_st = st;
        frag_color = color;
        gl_Position = proj_mat * vec4(position.xy, 0, 1);
    })";

    char frag_shader[] = R"(
    #version 330

    uniform sampler2D sampler;

    in vec2 frag_st;
    in vec4 frag_color;

    out vec4 out_color;

    void main() {
        out_color = frag_color * texture(sampler, frag_st);
    })";

    g_state.shader_program = eng::create_program(vert_shader, frag_shader);
    g_state.sampler_loc = glGetUniformLocation(g_state.shader_program, "sampler");
    g_state.proj_mat_loc = glGetUniformLocation(g_state.shader_program, "proj_mat");
    g_state.position_loc = glGetAttribLocation(g_state.shader_program, "position");
    g_state.st_loc = glGetAttribLocation(g_state.shader_program, "st");
    g_state.color_loc = glGetAttribLocation(g_state.shader_program, "color");
    glGenBuffers(1, &g_state.vbo);
    glGenBuffers(1, &g_state.ebo);
    glGenVertexArrays(1, &g_state.vao);

    glBindVertexArray(g_state.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
    glEnableVertexAttribArray(g_state.position_loc);
    glEnableVertexAttribArray(g_state.st_loc);
    glEnableVertexAttribArray(g_state.color_loc);

    glVertexAttribPointer(g_state.position_loc, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
                          (void *)offsetof(ImDrawVert, pos));
    glVertexAttribPointer(g_state.st_loc, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
                          (void *)offsetof(ImDrawVert, uv));
    glVertexAttribPointer(g_state.color_loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert),
                          (void *)offsetof(ImDrawVert, col));

    create_fonts_texture();

    // Restore the gl state prior to the call
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindVertexArray(last_vertex_array);
}

void invalidate_device_objects() {
    if (g_state.vao != 0) {
        glDeleteVertexArrays(1, &g_state.vao);
    }
    if (g_state.vbo != 0) {
        glDeleteBuffers(1, &g_state.vbo);
    }
    if (g_state.ebo != 0) {
        glDeleteBuffers(1, &g_state.ebo);
    }
    g_state.vao = 0;
    g_state.vbo = 0;
    g_state.ebo = 0;
    if (g_state.shader_program != 0) {
        glDeleteProgram(g_state.shader_program);
    }

    if (g_state.font_texture != 0) {
        glDeleteTextures(1, &g_state.font_texture);
        ImGui::GetIO().Fonts->TexID = 0;
        g_state.font_texture = 0;
    }
}

void create_fonts_texture() {
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels;
    int width, height;

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGenTextures(1, &g_state.font_texture);
    glBindTexture(GL_TEXTURE_2D, g_state.font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    io.Fonts->TexID = (void *)(intptr_t)g_state.font_texture;
    glBindTexture(GL_TEXTURE_2D, last_texture);
}

void render_drawlists(ImDrawData *draw_data) {
    // Avoid rendering when minimized, scale coordinates for retina displays
    // (screen coordinates != framebuffer coordinates)
    ImGuiIO &io = ImGui::GetIO();
    int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    // backup gl state
    GLint last_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_active_texture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &last_active_texture);
    GLint last_array_buffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    GLint last_element_array_buffer;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
    GLint last_vertex_array;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
    GLint last_blend_src;
    glGetIntegerv(GL_BLEND_SRC, &last_blend_src);
    GLint last_blend_dst;
    glGetIntegerv(GL_BLEND_DST, &last_blend_dst);
    GLint last_blend_equation_rgb;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &last_blend_equation_rgb);
    GLint last_blend_equation_alpha;
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &last_blend_equation_alpha);
    GLint last_viewport[4];
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

    // Setup render state: alpha-blending enabled, no face culling, no depth
    // testing, scissor enabled
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);

    glViewport(0, 0, fb_width, fb_height);

    // const auto ortho = math::orthographic_projection(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    // clang-format off
    const float ortho[4][4] = {
        {2.0f / io.DisplaySize.x, 0.0f,                     0.0f, 0.0f},
        {0.0f,                    2.0f / -io.DisplaySize.y, 0.0f, 0.0f},
        {0.0f,                    0.0f,                     -1.0f, 0.0f},
        {-1.0f,                   1.0f,                     0.0f, 1.0f},
    };
    // clang-format on

    glUseProgram(g_state.shader_program);
    glUniform1i(g_state.sampler_loc, 0);
    glUniformMatrix4fv(g_state.proj_mat_loc, 1, GL_FALSE, (float *)&ortho);
    glBindVertexArray(g_state.vao);

    // Draw each command list (in ImGui, each window has its own cmdlist)
    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        const ImDrawIdx *idx_buffer_offset = 0;

        glBindBuffer(GL_ARRAY_BUFFER, g_state.vbo);
        glBufferData(GL_ARRAY_BUFFER, cmd_list->VtxBuffer.Size * sizeof(ImDrawData), cmd_list->VtxBuffer.Data,
                     GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_state.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx),
                     cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        // Draw each command in this list
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
                glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w),
                          (int)(pcmd->ClipRect.z - pcmd->ClipRect.x),
                          (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
                glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount,
                               sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                               idx_buffer_offset);
            }
            idx_buffer_offset += pcmd->ElemCount;
        }
    }

    // Restore modified GL state
    glUseProgram(last_program);
    glActiveTexture(last_active_texture);
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindVertexArray(last_vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
    glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
    glBlendFunc(last_blend_src, last_blend_dst);
    if (last_enable_blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    if (last_enable_cull_face)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    if (last_enable_depth_test)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (last_enable_scissor_test)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2],
              (GLsizei)last_scissor_box[3]);
}

const char *get_clipboard_text(void *user_data) {
    GLFWwindow *window = (GLFWwindow *)user_data;
    return glfwGetClipboardString(window);
}

void set_clipboard_text(void *user_data, const char *text) {
    GLFWwindow *window = (GLFWwindow *)user_data;
    glfwSetClipboardString(window, text);
}

void *alloc_fn(size_t size) { return fo::memory_globals::default_allocator().allocate(size); }

void free_fn(void *p) {
    // p can be null as provided by ImGui
    fo::memory_globals::default_allocator().deallocate(p);
}