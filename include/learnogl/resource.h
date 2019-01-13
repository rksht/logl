// Do you even need a cache? If your simple as fuck but oh-so-fun game fits in memory, why the fuck do you
// need to implement an involved resource manager? It will do nothing my act as a tracker of data. It will not
// work.
#pragma once

#include <learnogl/constexpr_stuff.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <learnogl/string_table.h>
#include <scaffold/arena_allocator.h>

#include <functional>
#include <map>

#define AVG_RESOURCE_STRINGS_LENGTH 20
#define AVG_RESOURCE_KEYS 50

namespace eng {

// Kinds of resources
ENUMSTRUCT ResourceKind {
    enum E : u16 {
        INVALID,

        PNG,
        DDS,
        VERT,
        FRAG,
        COMP,
        GEOM,
        TESSC,
        TESSE,
        WAV,
        OGG,
        SJSON,
        INI,
        JSON,

        COUNT
    };

    static constexpr bool is_json(E e) { return e == SJSON || e == JSON; }
    static constexpr bool is_sound(E e) { return e == WAV || e == OGG; }
    static constexpr bool is_shader(E e) { return e >= VERT && e <= TESSE; }
    static constexpr bool is_texture(E e) { return e == PNG || e == DDS; }
};

namespace cexpr_internal {

using FileExtOfResource = CexprSparseArray<const char *, ResourceKind::COUNT, u16>;

constexpr inline auto gen_file_ext_of_resource() {
    FileExtOfResource map(nullptr);

    map.set(ResourceKind::PNG, "png");
    map.set(ResourceKind::DDS, "dds");
    map.set(ResourceKind::VERT, "vert");
    map.set(ResourceKind::FRAG, "frag");
    map.set(ResourceKind::COMP, "comp");
    map.set(ResourceKind::GEOM, "geom");
    map.set(ResourceKind::TESSC, "tessc");
    map.set(ResourceKind::TESSE, "tesse");
    map.set(ResourceKind::WAV, "wav");
    map.set(ResourceKind::OGG, "ogg");
    map.set(ResourceKind::SJSON, "sjson");
    map.set(ResourceKind::INI, "ini");
    map.set(ResourceKind::JSON, "json");

    return map;
}

} // namespace cexpr_internal

constexpr auto file_ext_of_resource = cexpr_internal::gen_file_ext_of_resource();

struct ResourceExtraDataHeader {
    u32 size;
};

struct ResourceHandle {
    // Buffer for storage of this resource. Null will indicate that resource is either not needed to be loaded
    // or already loaded and this handle exists only for the associated extra data.
    u8 *buffer;
    u32 buffer_bytes;

    u8 *extra_data_ptr;

    u8 allocator_index;
    u8 before_unload_callback_index;
};

using BeforeResourceUnload_Callback = std::function<void(ResourceHandle *resource_handle)>;

inline bool uninited_resource_handle(const ResourceHandle &h) {
    return h.buffer_bytes == std::numeric_limits<u32>::max();
}

// Well, what can you do?
template <typename ExtraDataType> ExtraDataType *extra_data_for_resource(ResourceHandle &res_handle) {
    return reinterpret_cast<ExtraDataType *>(res_handle.extra_data_ptr);
}

// The global resource manager present as a member of GLApp. Loading of resources into this structure is
// implemented by different systems (audio and renderer).
struct ResourceManager {
    // Used to intern strings
    StringTable _resource_strings;

    // Root directory where resource files are found
    std::vector<fs::path> _resource_roots;

    // Extra data for resources get allocated from this arena
    fo::ArenaAllocator _extra_data_allocator{ fo::memory_globals::default_allocator(), 4 * 1024 };

    std::map<std::string, ResourceHandle> _dic_path_to_handle;

    std::array<fo::Allocator *, 8> _resource_allocators{};

    std::array<BeforeResourceUnload_Callback, 8> _before_unload_callbacks;

    reallyconst extra_data_alloc_index = 0;

    ResourceManager()
        : CTOR_INIT_FIELD(_resource_strings, AVG_RESOURCE_STRINGS_LENGTH, AVG_RESOURCE_KEYS) {

        // Extra data allocators
        for (int i = 0; i < (int)_resource_allocators.size(); ++i) {
            _resource_allocators[i] = &fo::memory_globals::default_allocator();
        }

        _resource_allocators[0] = &_extra_data_allocator;
    }
};

fs::path get_path_to_resource(const ResourceManager &self, const char *relative_path_to_resource);

inline void add_resource_root(ResourceManager &self, fs::path root_dir) {
    self._resource_roots.push_back(root_dir);
}

// Loads a resource. Returns true on success and initializes the `out_resource_handle` structure. Otherwise
// returns false.
bool load_resource(ResourceManager &c,
                   const char *relative_path_to_resource,
                   ResourceHandle *out_resource_handle,
                   bool32 get_format_from_ext = true);

// Unloads the given resource. Currently a no-op. All resources are loaded, but freed altogether. void
void unload_resource(ResourceManager &c, const char *relative_path_to_resource);

// Unloads every resource. Mostly a no-op. If reallocate_memory is true, will reallocate the memory required
// to hold extra data and all that.
void unload_every_resource(ResourceManager &c, bool reallocate_memory = false);

} // namespace eng
