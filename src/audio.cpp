#include <learnogl/audio.h>

namespace eng {

struct SoundManagerImpl {};

SoundManager::SoundManager() {}

SoundManager::~SoundManager() {}

void init_sound_manager(SoundManager &self, GLFWwindow *glfw_window) {
    self.impl = std::make_unique<SoundManagerImpl>();
    // init_sound_manager_impl(self.impl.get(), glfw_window);
}

void close_sound_manager(SoundManager &m) {
    // close_sound_manager_impl(m.impl.get());
    m.impl = nullptr;
}

ResourceHandle *load_wav_file(const char *relative_path) { return nullptr; }

} // namespace eng
