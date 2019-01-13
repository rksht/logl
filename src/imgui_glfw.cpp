#include <learnogl/imgui_glfw.h>
#include <learnogl/gl_misc.h>

namespace eng {

namespace imgui_glfw {

struct ImGui_Context_Impl {
    fo::Allocator *_memory_allocator = nullptr;
    InputCallbacksFlags _callbacks_installed = all_callbacks;
    GLFWwindow *_glfw_window = nullptr;
};

TU_LOCAL init_imgui_io(ImGui_Context_Impl &pimpl);
TU_LOCAL init_render_callback(ImGui_Context_Impl &pimpl);

void init(ImGui_Context &imgui_context,
          GLFWwindow *window = gl().window,
          InputCallbacksFlags callbacks = all_callbacks,
          fo::Allocator &allocator = fo::memory_globals::default_allocator())
{
    imgui_context._pimpl = fo::make_new<ImGui_Context_Impl>(fo::memory_globals::default_allocator());
    _pimpl->_glfw_window = window;
    _pimpl->_allocator = &allocator;
    _pimpl->_callbacks_installed = callbacks;
}

}

} // namespace eng