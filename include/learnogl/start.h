#pragma once

#include <learnogl/essential_headers.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <scaffold/string_id.h>

namespace eng {

inline void init_memory(u32 scratch_buffer_size = 4 * 1024) {

// Turn off that damn MSVC crt heap debugging functionality.
#if defined(_MSC_VER)
    _CrtSetDbgFlag(0);
#endif

    fo::memory_globals::InitConfig mem_config;
    mem_config.scratch_buffer_size = scratch_buffer_size;
    fo::memory_globals::init(mem_config);
    init_pmr_globals();
}

inline void shutdown_memory() {
    shutdown_pmr_globals();
    fo::memory_globals::shutdown();
}
} // namespace eng
