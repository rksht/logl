#include <learnogl/file_monitor.h>
#include <scaffold/temp_allocator.h>

#define FOUND_IMPLEMENTATION 0

#if __has_include(<sys/inotify.h>)
#    define INOTIFY_AVAILABLE 1
#    undef FOUND_IMPLEMENTATION
#    define FOUND_IMPLEMENTATION 1

#    include <fcntl.h>
#    include <sys/inotify.h>
#    include <sys/stat.h>
#    include <type_traits>
#    include <unistd.h>

#endif

using namespace fo;

// Linux implementation
#ifdef INOTIFY_AVAILABLE

namespace eng::logl_internal {

struct SymbolInfo {
    eng::StringSymbol pathsymbol;
    int watch_desc;
};

using PathAssoc = std::vector<eng::FileMonitor::ListenerFn>;

struct FileMonitorImpl {
    // Map from path -> watch descriptor
    std::vector<SymbolInfo> _path_info;

    // Mapping from watchdesc to the list of listeners listening to the file
    std::unordered_map<int, PathAssoc> _path_listeners;
    int _inotify_fd;

    // Ctor
    FileMonitorImpl();

    // Dtor
    ~FileMonitorImpl();

    FileMonitorImpl(const FileMonitorImpl &) = delete;
    FileMonitorImpl(FileMonitorImpl &&) = delete;

    // Associates a new listener for the given pathname
    void add_listener(const char *pathname, eng::FileMonitor::ListenerFn listener_fn);

    // Poll changes on files. Returns the number of calls to listeners
    u32 poll_changes();
};

// Just for reference
static constexpr u32 k_inotify_flags = IN_DELETE_SELF | IN_CLOSE_WRITE;

static inline constexpr eng::FileMonitor::EventType inotify_to_ours(u32 inotify_mask) {
    if (inotify_mask & IN_DELETE_SELF) {
        return eng::FileMonitor::EventType::deleted;
    }
#    if 0
    if (inotify_mask & IN_MODIFY) {
        return eng::FileMonitor::EventType::modified;
    }
#    endif
    if (inotify_mask & IN_CLOSE_WRITE) {
        return eng::FileMonitor::EventType::modified;
    }
    return eng::FileMonitor::EventType::irrelevant;
}

FileMonitorImpl::FileMonitorImpl() {
    _inotify_fd = inotify_init();

    CHECK_F(_inotify_fd != -1, "Failed to initialize inotify");

    int flags = fcntl(_inotify_fd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(_inotify_fd, F_SETFL, flags);
}

FileMonitorImpl::~FileMonitorImpl() {
    LOG_IF_F(ERROR, close(_inotify_fd) == -1, "Failed to close inotify descriptor");
}

static optional<SymbolInfo> find_symbol_info(const FileMonitorImpl &fm, eng::StringSymbol pathsymbol) {
    for (size_t i = 0; i < fm._path_info.size(); ++i) {
        if (fm._path_info[i].pathsymbol == pathsymbol) {
            return fm._path_info[i];
        }
    }
    return nullopt;
}

static optional<SymbolInfo> find_symbol_info_of_wd(const FileMonitorImpl &fm, int wd) {
    for (size_t i = 0; i < fm._path_info.size(); ++i) {
        if (fm._path_info[i].watch_desc == wd) {
            return fm._path_info[i];
        }
    }
    return nullopt;
}

static optional<SymbolInfo> add_watch(FileMonitorImpl &fm, const char *path_cstr) {
    const auto pathsymbol = eng::default_string_table().to_symbol(path_cstr);
    // If this path is already open, return the filedesc, just return that, otherwise open it, add a watch and
    // cache it.
    auto symbol_info = find_symbol_info(fm, pathsymbol);
    if (symbol_info) {
        return symbol_info.value();
    }

    int wd = inotify_add_watch(fm._inotify_fd, path_cstr, k_inotify_flags);
    if (wd == -1) {
        LOG_F(ERROR, "Failed to add watch for path: %s", path_cstr);
        return nullopt;
    }

    fm._path_info.push_back(SymbolInfo { pathsymbol, wd });
    LOG_F(INFO, "[FileMonitor] Added new file to monitor: %s", path_cstr);
    return fm._path_info.back();
}

void FileMonitorImpl::add_listener(const char *pathname, eng::FileMonitor::ListenerFn listener_fn) {
    auto maybe_symbol_info = add_watch(*this, pathname);

    // Failed to add watch
    if (!maybe_symbol_info) {
        return;
    }

    auto &symbol_info = maybe_symbol_info.value();
    auto present = _path_listeners.find(symbol_info.watch_desc);

    if (present == _path_listeners.end()) {
        present = _path_listeners.insert(std::make_pair(symbol_info.watch_desc, PathAssoc {})).first;
    }
    present->second.push_back(std::move(listener_fn));
}

u32 FileMonitorImpl::poll_changes() {
    TempAllocator1024 ta(memory_globals::default_allocator());
    Array<u8> paths_changed(ta, 1000);

    u32 num_listeners_called = 0;

    while (true) {
        auto read_bytes = read(_inotify_fd, data(paths_changed), vec_bytes(paths_changed));

        if (read_bytes == -1 && errno == EAGAIN) {
            return num_listeners_called;
        }

        if (read_bytes == -1) {
            LOG_F(ERROR, "[FileMonitor] - Failed to read events");
            abort();
        }

        if (read_bytes == 0) {
            return num_listeners_called;
        }

        LOG_F(INFO, "[FileMonitor] - Got some file events");

        u8 *end = data(paths_changed) + read_bytes;
        u8 *p_next = data(paths_changed);

        // Read each event from this read batch
        while (p_next < end) {
            auto p_event = (struct inotify_event *)p_next;
            p_next += sizeof(struct inotify_event) + p_event->len;

            // Read this event and call the listeners
            auto event_type = inotify_to_ours(p_event->mask);

            if (event_type != eng::FileMonitor::EventType::irrelevant) {
                auto it = _path_listeners.find(p_event->wd);

                auto symbol_info = find_symbol_info_of_wd(*this, p_event->wd).value();
                auto pathname = eng::default_string_table().to_string(symbol_info.pathsymbol);

                if (it == _path_listeners.end()) {
                    LOG_F(ERROR, "Watch descriptor not registered with any listener");
                } else {
                    for (auto &fn : it->second) {
                        ++num_listeners_called;
                        ::invoke(
                            fn,
                            eng::FileMonitor::ListenerArgs { event_type, symbol_info.pathsymbol, pathname });
                    }
                }
            }
        }
    }
}

} // namespace eng::logl_internal

#endif

// Windows implementation

#if !(FOUND_IMPLEMENTATION)
#    ifdef WIN32
#        include <windows.h>
#        undef FOUND_IMPLEMENTATION
#        define FOUND_IMPLEMENTATION 1

namespace eng::logl_internal {

using NativeString = decltype(fs::path().native());

// Represents a single file being watched
struct FilePathRepr {
    // This file's path in generic format
    eng::StringSymbol pathsymbol;

    // This file's path in native format
    NativeString pathname_native;

    // All the listners for this file
    std::vector<FileMonitor::ListenerFn> listeners;
};

// Per-directory info
struct DirWatchInfo {
    HANDLE watch_handle;

    // Generic pathname's symbol
    eng::StringSymbol dirpath_symbol;

    // All the files in this dir for which we will call listeners
    std::vector<FilePathRepr> files_in_dir;
};

struct FileMonitorImpl {
    std::vector<DirWatchInfo> _dir_watch_infos;

    void add_listener(const char *pathname, FileMonitor::ListenerFn listener_fn);
    u32 poll_changes();
};

// Creates a new watch and returns the index of the watch info in the vector
size_t add_new_dir_watch(FileMonitorImpl &fm, eng::StringSymbol dirpath_symbol, const char *dirpath_cstr) {
    // dirpath_cstr is in generic format. We translate to native. Simple way to do it is just use fs::path.
    fs::path dirpath(dirpath_cstr);

    const auto dirpath_str = dirpath.native();
    auto dirpath_native_cstr = dirpath_str.c_str();

    HANDLE watch_handle =
        CreateFileW(dirpath_native_cstr,
                    FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                    NULL);

    if (watch_handle == INVALID_HANDLE_VALUE) {
        LOG_F(ERROR, "Failed to add watch for directory: %s", dirpath_cstr);
        return ~size_t(0);
    }

    fm._dir_watch_infos.push_back(DirWatchInfo { watch_handle, dirpath_symbol, {} });

    LOG_F(INFO, "Added watch on directory: %s", dirpath_cstr);

    return fm._dir_watch_infos.size() - 1;
}

void FileMonitorImpl::add_listener(const char *pathname, FileMonitor::ListenerFn listener_fn) {
    // First thing is to take out the directory name

    LOG_F(WARNING, "[FileMonitor] - Not implemented on windows");

#        if 0
    fs::path dirpath(pathname);

    if (!fs::exists(dirpath)) {
        LOG_F(ERROR, "[FileMonitor] File %s does not exist", pathname);
        return;
    }
    dirpath.remove_filename();
    auto dirpathstring = dirpath.generic_u8string();

    // Add the file to the directory being watched
    auto filepath_symbol = eng::default_string_table().to_symbol(pathname);
    auto dirpath_symbol = eng::default_string_table().to_symbol(dirpathstring.c_str());

    // Search for the directory if we are already watching it
    size_t info_index = ~size_t(0);
    for (size_t i = 0; i < _dir_watch_infos.size(); ++i) {
        if (_dir_watch_infos[i].dirpath_symbol == dirpath_symbol) {
            info_index = i;
            break;
        }
    }
    if (info_index == ~size_t(0)) {
        info_index = add_new_dir_watch(*this, dirpath_symbol, dirpathstring.c_str());
        if (info_index == ~size_t(0)) {
            return;
        }
    }
    auto &watch_info = _dir_watch_infos[info_index];

    LOG_F(INFO, "Adding listener to file: %s", eng::default_string_table().to_string(filepath_symbol));

    // See if this file is already being monitored, if yes then add the listener to it.
    for (auto &file_repr : watch_info.files_in_dir) {
        if (file_repr.pathsymbol == filepath_symbol) {
            file_repr.listeners.push_back(std::move(listener_fn));
            return;
        }
    }

    // Otherwise create a new FilePathRepr
    watch_info.files_in_dir.push_back(FilePathRepr{filepath_symbol, fs::path(pathname).native(),
                                                   std::vector<FileMonitor::ListenerFn>{listener_fn}});

#        endif
}

// void overlapped_completion_routine(DWORD error_code, DWORD bytes_transferred, LPOVERLAPPED p_overlapped) {}

u32 FileMonitorImpl::poll_changes() {
#        if 0
    TempAllocator1024 ta(memory_globals::default_allocator());
    Array<u8> buffer(ta, 1000);

    for (auto &watch_info : _dir_watch_infos) {
        u8 *p_buffer = data(buffer);

        DWORD bytes_returned;
        // OVERLAPPED overlapped{};

        // Keep reading pending directory changes
        while (ReadDirectoryChangesW(watch_info.watch_handle, p_buffer, vec_bytes(buffer), false,
                                     FILE_NOTIFY_CHANGE_LAST_WRITE, &bytes_returned, nullptr, nullptr)) {
            LOG_F(INFO, "Changes in directory: %s",
                  eng::default_string_table().to_string(watch_info.dirpath_symbol));
        }
    }
#        endif
    return 0;
}

} // namespace logl_internal

#    endif
#endif

#if !(FOUND_IMPLEMENTATION)
#    error "Haven't implemented FileMonitor on this platform"
#endif

namespace eng {

FileMonitor::FileMonitor()
    : _impl(new logl_internal::FileMonitorImpl) {}

FileMonitor::~FileMonitor() {}

void FileMonitor::add_listener(const char *pathname, FileMonitor::ListenerFn listener_fn) {
    _impl->add_listener(pathname, std::move(listener_fn));
}

u32 FileMonitor::poll_changes() { return _impl->poll_changes(); }

static std::aligned_storage_t<sizeof(FileMonitor)> fm_storage[1];

void init_default_file_monitor() { new (fm_storage) FileMonitor(); }

FileMonitor &default_file_monitor() { return *reinterpret_cast<FileMonitor *>(&fm_storage[0]); }

void close_default_file_monitor() { default_file_monitor().~FileMonitor(); }

} // namespace eng