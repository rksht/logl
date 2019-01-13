#pragma once

#include <learnogl/kitchen_sink.h>
#include <learnogl/string_table.h>

#include <loguru.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

namespace eng::logl_internal {

// Forward decl for the actual implementation struct
struct FileMonitorImpl;

} // namespace logl_internal

namespace eng {

struct FileMonitor {

    enum class EventType : u32 { modified, deleted, irrelevant };

    struct ListenerArgs {
        EventType event_type;
        StringSymbol pathsym;
        const char *pathname;
    };

    using ListenerFn = std::function<void(ListenerArgs)>;

    struct SymbolInfo {
        StringSymbol symbol;
        int watch_desc;
    };

    std::unique_ptr<eng::logl_internal::FileMonitorImpl> _impl;

    // Ctor
    FileMonitor();

    // Dtor
    ~FileMonitor();

    FileMonitor(const FileMonitor &) = delete;
    FileMonitor(FileMonitor &&) = delete;

    // Associates a new listener for the given pathname
    void add_listener(const char *pathname, ListenerFn listener_fn);

    // Poll changes on files. Returns the number of calls to listeners
    u32 poll_changes();
};

// Initializes a global default file monitor
void init_default_file_monitor();

// Returns a global default file monitor
FileMonitor &default_file_monitor();

// Deletes the default file monitor
void close_default_file_monitor();

} // namespace eng
