#include <learnogl/audio.h>
#include <learnogl/resource.h>

namespace eng {

static ResourceKind::E get_resource_kind_from_ext(const char *ext) {
    for (u16 i = 1; i < ResourceKind::COUNT; ++i) {
        const char *ext_of_i = file_ext_of_resource.get_maybe_nil(ResourceKind::E(i));
        assert(ext_of_i != nullptr);
        if (strcmp(ext_of_i, ext) == 0) {
            return ResourceKind::E(i);
        }
    }
    return ResourceKind::INVALID;
}

fs::path get_path_to_resource(const ResourceManager &self, const char *relative_path_to_resource) {
    fs::path fullpath;

    for (LET &root : self._resource_roots) {
        auto p = root / relative_path_to_resource;
        if (fs::exists(p)) {
            fullpath = std::move(p);
        }
    }

    if (fullpath.empty()) {
        LOG_F(ERROR, "Resource file '%s' not found in any of the resource roots", relative_path_to_resource);
    }

    return fullpath;
}

bool load_resource(ResourceManager &self,
                   const char *relative_path_to_resource,
                   ResourceHandle *out_resource_handle,
                   bool32 get_format_from_ext) {
    assert(get_format_from_ext);

    fs::path fullpath = get_path_to_resource(self, relative_path_to_resource);

    if (fullpath.empty()) {
        return false;
    }

    LET ext = fullpath.extension();

    auto u8string = ext.generic_u8string();

    if (!(u8string.c_str() + 1)) {
        LOG_F(ERROR, "Failed to load resource '%s' -- empty extension", fullpath.u8string().c_str());
        return false;
    }

    ResourceKind::E resource_kind = get_resource_kind_from_ext(&u8string.c_str()[1]);
    // ^ extension() returns with the '.' at the beginning, so starting after that.

    const char *fail_reason = nullptr;

    switch (resource_kind) {
    default: { fail_reason = "Unimplemented loader"; } break;

    case ResourceKind::INVALID: {
        fail_reason = "Unrecognized extension";
    } break;

    case ResourceKind::WAV: {
        load_wav_file(relative_path_to_resource);
    } break;
    }

    if (fail_reason) {
        LOG_F(ERROR, "Failed to load resource '%s' -- %s", fullpath.u8string().c_str(), fail_reason);
        return false;
    } else {
        LOG_F(INFO, "Loaded resource '%s'", fullpath.generic_u8string().c_str());
    }

    return true;
}

void unload_resource(ResourceManager &c, const char *relative_path_to_resource) {
    // nop.
}

void unload_every_resource(ResourceManager &self, bool reallocate_memory) {
    for (auto &kv : self._dic_path_to_handle) {
        if (uninited_resource_handle(kv.second)) {
            continue;
        }

        auto &before_unload = self._before_unload_callbacks[kv.second.before_unload_callback_index];
        if (before_unload) {
            before_unload(&kv.second);
        }
    }
}

} // namespace eng
