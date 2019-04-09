#include <learnogl/file_monitor.h>
#include <scaffold/temp_allocator.h>

struct DummyFileMonitorImpl {
    DummyFileMonitorImpl() = default;

    DummyFileMonitorImpl(const DummyFileMonitorImpl &) = delete;
    DummyFileMonitorImpl(DummyFileMonitorImpl &&) = delete;

    // Associates a new listener for the given pathname
    void add_listener(const char *pathname, eng::FileMonitor::ListenerFn listener_fn) {
        UNUSED(pathname);
        UNUSED(listener_fn);
    }

    // Poll changes on files. Returns the number of calls to listeners
    u32 poll_changes() {}
};

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

    fm._path_info.push_back(SymbolInfo{ pathsymbol, wd });
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
        present = _path_listeners.insert(std::make_pair(symbol_info.watch_desc, PathAssoc{})).first;
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
                            eng::FileMonitor::ListenerArgs{ event_type, symbol_info.pathsymbol, pathname });
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
#    if defined(WIN32)
#        undef FOUND_IMPLEMENTATION
#        define FOUND_IMPLEMENTATION 1

namespace eng::logl_internal {
struct FileMonitorImpl : DummyFileMonitorImpl {};

} // namespace eng::logl_internal

#    endif
#endif

// MacOS implementation
#if !(FOUND_IMPLEMENTATION)

#    if defined(__APPLE__)

namespace eng::logl_internal {
struct FileMonitorImpl : DummyFileMonitorImpl {};

} // namespace eng::logl_internal

#define FOUND_IMPLEMENTATION 1

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
