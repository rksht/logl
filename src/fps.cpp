#include <learnogl/fps.h>

#include <assert.h>
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>

static constexpr int32_t k_title_buffer_size = 128;
static constexpr float k_alpha = 0.1f;

struct FpsTitle {
    float last_dt;
    char *fps_ptr; // Remaining space in title buffer
    char title_buffer[k_title_buffer_size];
};

static std::aligned_storage_t<sizeof(FpsTitle), alignof(FpsTitle)> static_fps_title[1];

FpsTitle &get_fps_title() { return *reinterpret_cast<FpsTitle *>(static_fps_title); }

void fps_title_init(const char *title) {
    auto &f = get_fps_title();

    int32_t num_written = snprintf(f.title_buffer, sizeof(f.title_buffer), "%s - ", title);
    assert(k_title_buffer_size - num_written >= 20 && "Title too long");

    f.last_dt = 0.0f;
    f.fps_ptr = f.title_buffer + num_written;
}

void fps_title_update(GLFWwindow *w, float dt) {
    auto &f = get_fps_title();
    assert(f.title_buffer[0] && "Uninitialized?");

    f.last_dt = (1.0f - k_alpha) * f.last_dt + k_alpha * dt;

    const auto n = &f.title_buffer[k_title_buffer_size] - f.fps_ptr;

    memset(f.fps_ptr, 0, n);
    snprintf(f.fps_ptr, n, "%.3f fps", 1.0f / f.last_dt);

    glfwSetWindowTitle(w, f.title_buffer);
}
