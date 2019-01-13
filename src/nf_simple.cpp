#include <learnogl/nf_simple.h>
#include <scaffold/array.h>
#include <scaffold/memory.h>

#include <fmt/format.h>

#include <mutex> // once_flag

void *simple_nf_realloc(
    void *user_data, void *prev_pointer, int orig_size, int new_size, const char *file, int line) {
    (void)user_data;
    (void)orig_size;
    (void)file;
    (void)line;
    return realloc(prev_pointer, new_size);
}

nfcd_ConfigData *simple_parse_cstr(const char *src, bool abort_on_error) {
    nfcd_ConfigData *cd = nfcd_make(simple_nf_realloc, nullptr, 0, 0);
    nfjp_Settings settings = simple_nfjson();
    const char *ret = nfjp_parse_with_settings(src, &cd, &settings);
    if (ret != nullptr) {
        log_err("Failed to parse config data - %s", ret);
        if (abort_on_error) {
            abort();
        } else {
            return nullptr;
        }
    }
    return cd;
}

nfcd_ConfigData *simple_parse_file(const char *file_path, bool abort_on_error) {
    fo::Array<uint8_t> buf(fo::memory_globals::default_allocator());
    read_file(file_path, buf, true);
    const char *src = (const char *)fo::data(buf);
    return simple_parse_cstr(src, abort_on_error);
}

const char *simple_to_string(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) {
    bool success = (nfcd_type(cd, loc) == NFCD_TYPE_STRING);
    if (abort_on_error) {
        log_assert(success, "NFCD location does not refer to a string");
    }
    return nfcd_to_string(cd, loc);
}

nfcd_loc simple_get_qualified(nfcd_ConfigData *cd, const char *qualified_name) {
    fo::TempAllocator1024 ta;
    fo::Vector<fo_ss::Buffer> split_buffers(ta);
    fo::reserve(split_buffers, 8);
    split_string(qualified_name, '.', split_buffers, ta);

    nfcd_loc cur_root = nfcd_root(cd);

    for (auto &name : split_buffers) {
        if (nfcd_type(cd, cur_root) != NFCD_TYPE_OBJECT) {
            LOG_F(WARNING,
                  "Not a JSON object - %s - while iterating the object %s",
                  fo_ss::c_str(name),
                  qualified_name);
            return nfcd_null();
        }

        nfcd_loc loc = nfcd_object_lookup(cd, cur_root, fo_ss::c_str(name));
        if (loc == nfcd_null()) {
            LOG_F(WARNING,
                  "Did not find the key %s while getting the full object %s",
                  fo_ss::c_str(name),
                  qualified_name);
            return nfcd_null();
        }
        cur_root = loc;
    }

    return cur_root;
}

fs::path simple_absolute_path(const char *path_relative_to_project) {
    static std::once_flag call_me_once;
    static fs::path project_path;

    std::call_once(call_me_once, []() {
        project_path = __FILE__;
        CHECK_F(project_path.has_root_path(), "__FILE__ is not absolute path");
        project_path = project_path.parent_path().parent_path();
    });

    return project_path / path_relative_to_project;
}

namespace inistorage {

static void fill_storage_from_subobject(
    Storage &self, std::string prefix, nfcd_ConfigData *cd, nfcd_loc r, const fs::path &ini_file) {

    int key_count = nfcd_object_size(cd, r);

    for (int i = 0; i < key_count; ++i) {
        struct nfcd_ObjectItem *kv = nfcd_object_item(cd, r, i);
        std::string key_string(nfcd_to_string(cd, kv->key));
        if (prefix.size() != 0) {
            key_string = prefix + std::string(".") + key_string; // Fully qualified name
        }

        Variant value;

        bool unexpected_type = false;
        bool is_sub_object = false;

        switch (nfcd_type(cd, kv->value)) {
        case NFCD_TYPE_STRING: {
            value = std::string(nfcd_to_string(cd, kv->value));
        } break;
        case NFCD_TYPE_FALSE: {
            value = false;
        } break;
        case NFCD_TYPE_TRUE: {
            value = true;
        } break;
        case NFCD_TYPE_NUMBER: {
            value = nfcd_to_number(cd, kv->value);
        } break;

        case NFCD_TYPE_ARRAY: {
            int len = nfcd_array_size(cd, kv->value);
            if (len == 2) {
                value = SimpleParse<fo::Vector2>::parse(cd, kv->value);
            } else if (len == 3) {
                value = SimpleParse<fo::Vector3>::parse(cd, kv->value);
            } else if (len == 4) {
                value = SimpleParse<fo::Vector4>::parse(cd, kv->value);
            } else {
                LOG_F(ERROR, "Array of size %i not allowed. Only 2, 3, 4 sized arrays allowed", len);
                unexpected_type = true;
            }

        } break;

        case NFCD_TYPE_OBJECT: {
            fill_storage_from_subobject(self, key_string, cd, kv->value, ini_file);
            is_sub_object = true;
        } break;

        default:
            LOG_F(ERROR,
                  "Ignoring unexpected value for key %s in ini file %s",
                  key_string.c_str(),
                  ini_file.u8string().c_str());
            unexpected_type = true;
        }

        if (!unexpected_type && !is_sub_object) {
            self._map.insert(std::make_pair(key_string, value));
        }
    }
}

Storage::Storage(const fs::path &path, bool keep_config_data) { SELF.init_from_file(path, keep_config_data); }

void Storage::init_from_file(const fs::path &path, bool keep_config_data) {
    if (_initialized) {
        _map.clear();
        _initialized = false;
        if (_cd) {
            nfcd_free(_cd);
        }
    }

    auto cd = simple_parse_file(path.u8string().c_str(), true);

    DEFER([&]() {
        if (!keep_config_data) {
            nfcd_free(cd);
        }
    });

    auto r = nfcd_root(cd);

    fill_storage_from_subobject(*this, "", cd, r, path);

    for (auto &kv : _map) {
        printf("%s, ", kv.first.c_str());
    }
    puts("\n");

    _initialized = true;
}

} // namespace inistorage
