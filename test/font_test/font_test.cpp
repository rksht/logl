#include "essentials.h"
#include <learnogl/font.h>

using namespace fo;

int main() {
    memory_globals::init();
    DEFER( []() { memory_globals::shutdown(); });

    GLFWwindow *window;
    const int window_width = 1024;
    const int window_height = 640;

    eng::start_gl(&window, window_width, window_height, "font_test", 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    font::FontData fd;

    std::vector<font::CodepointRange> codepoint_ranges{{0, 128}};

    init(fd, LOGL_UI_FONT, 40, codepoint_ranges.data(), codepoint_ranges.size());

    glfwTerminate();
}
