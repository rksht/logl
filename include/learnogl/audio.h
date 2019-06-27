// Audio system. Contains active sounds and abstracts different audio implementations

#if 0
#pragma once

#include <learnogl/kitchen_sink.h>
#include <learnogl/resource.h>

struct GLFWwindow; // Forward decl

namespace eng {

enum SoundFileFormat { WAV, OGG };

struct SoundManagerImpl;

struct AudioBufferCommon {
    bool is_paused;
    bool is_looping;
    i32 volume;
};

struct SoundManager {
    std::unique_ptr<SoundManagerImpl> impl;
    std::vector<ResourceHandle *> active_sounds;

    SoundManager();
    ~SoundManager();
};

void init_sound_manager(SoundManager &self, GLFWwindow *glfw_window);
void close_sound_manager(SoundManager &self);

// -- Sound file loader function

// Loads the wav file into the resource manager
ResourceHandle *load_wav_file(const char *relative_path);

} // namespace eng

#if defined(WIN32)

#else
#    warning "Audio impl in the cards"

#endif

#endif
