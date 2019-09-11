#include <learnogl/nf_simple.h>
#include <scaffold/array.h>
#include <scaffold/memory.h>
#include <scaffold/scanner.h>

#include <fmt/format.h>

#include <mutex> // once_flag
#include <string>

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

Storage::Storage(const fs::path &path, bool keep_config_data) {
    this->init_from_file(path, keep_config_data);
}

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

void Storage::merge(const Storage &other) {
    for (auto &e : other._map) {
        _map[e.first] = e.second;
    }
}

struct ArgumentParser {
    fo::TempAllocator<1024> _temp_object_allocator;
    scanner::Scanner _sc;

    int _arg_number = 0;
    const char *_argument = nullptr;

    nfcd_ConfigData *_cd = nullptr;

    struct TempObject;
    using TempValue = VariantTable<nfcd_loc, TempObject *>;

    struct TempObject {
        fo::OrderedMap<std::string, TempValue> m;
    };

    TempObject *_root;

    // Current argument
    int _ac;
    char **_av;

    void _delete_temp_object(TempObject *root) {
        for (auto &e : root->m) {
            auto &mapped_value = e.second();

            if (mapped_value.contains_subtype<TempObject *>()) {
                _delete_temp_object(mapped_value.get_value<TempObject *>());
            }
        }
        root->~TempObject();
    }

    ArgumentParser(int ac, char **av)
        : _sc()
        , _ac(ac)
        , _av(av) {
        // scanner::init_from_cstring(_sc, argument);
        _root = fo::make_new<TempObject>(_temp_object_allocator);
    }

    ~ArgumentParser() {
        if (_root) {
            _delete_temp_object(_root);
        }
    }

#if 0
    void dump_temp(TempValue root, fo_ss::Buffer &ss) {
        if (root.contains_subtype<nfcd_loc>()) {
            ss = stringify_nfcd(_cd, root.get_value<nfcd_loc>(), std::move(ss));
        } else {
            ss << "{";

            auto temp_object = root.get_value<TempObject *>();

            for (auto &entry : temp_object->m) {
                ss << entry.first().c_str() << ": ";
                dump_temp(entry.second(), ss);
                ss << ", ";
            }

            ss << "}";
        }
    }

#endif

    void parse(nfcd_ConfigData **out_cd) {
        _cd = *out_cd;

        DEFERSTAT(*out_cd = _cd);

        for (int i = 1; i < _ac; ++i) {
            _arg_number = i;
            _argument = _av[i];
            _parse_current_argument();
        };

        nfcd_loc root_loc = _temp_to_nfcd(_root);
        nfcd_set_root(_cd, root_loc);
    }

    // Grammar
    // keyvalue: IDENTIFER ('.' IDENTIFER)* '='' Array | NUMBER | STRING
    // Array: [NUMBER, NUMBER (, NUMBER, (NUMBER))]
    void _parse_current_argument() {
        scanner::init_from_cstring(_sc, _argument);

        auto parse_key_return = parse_key();

        if (_sc.current_tok != '=') {
            error_message("Expected '=' after key", true);
        }

        scanner::next(_sc);

        auto value = parse_value();

#if 0
        {
            auto ss = stringify_nfcd(_cd, value);
            LOG_F(INFO, "Value parsed = %s", fo_ss::c_str(ss));
            LOG_F(INFO, "Key parsed = %s", parse_key_return.key.c_str());

            // ss = stringify_nfcd(_cd, parse_key_return.last_containing_object);
            // LOG_F(INFO, "Object to put in = %s", fo_ss::c_str(ss));
        }

#endif

        fo::set(parse_key_return.last_containing_object->m, parse_key_return.key, value);

        scanner::next(_sc);
    }

    nfcd_loc _temp_to_nfcd(const TempObject *root) {
        // Convert the temp object to nfcd object

        nfcd_loc object = nfcd_add_object(&_cd, fo::size(root->m));

        for (const auto &e : root->m) {
            const auto key = e.first();
            const auto value = e.second();

            if (value.contains_subtype<nfcd_loc>()) {
                nfcd_set(&_cd, object, key.c_str(), value.get_value<nfcd_loc>());
            } else {
                nfcd_loc subobject = _temp_to_nfcd(value.get_value<TempObject *>());
                nfcd_set(&_cd, object, key.c_str(), subobject);
            }
        }
        { auto ss = stringify_nfcd(_cd, object); }

        return object;
    }

    void error_message(std::string message, bool abort = false) {
        int pos = _sc.token_start;
        LOG_F(ERROR,
              "Argument parse error %i, (: %s) - at position - %i. %s",
              _arg_number,
              _argument,
              pos,
              message.c_str());

        if (abort) {
            ABORT_F("See log.");
        }
    }

    struct ParseKeyReturn {
        std::string key;
        TempObject *last_containing_object;
    };

    ParseKeyReturn parse_key() {
        fo::TempAllocator1024 ta;

        int token = scanner::next(_sc);

        ParseKeyReturn ret;
        ret.last_containing_object = _root;

        if (token == scanner::IDENT) {
            ret.key = scanner::token_text(_sc, ta);
        } else {
            error_message("Expected an identifier while parsing key", true);
        }

        while (scanner::next(_sc) == '.') {
            // Make an object for the last sub name
            // nfcd_loc child_object = nfcd_add_object(&_cd, 1);
            //

            // Check if already have the object
            auto it = fo::get(ret.last_containing_object->m, ret.key);
            TempObject *child_object = nullptr;

            if (it == fo::end(ret.last_containing_object->m)) {
                child_object = fo::make_new<TempObject>(fo::memory_globals::default_allocator());
            } else {
                auto existing = it->second();
                if (existing.contains_subtype<nfcd_loc>()) {
                    auto ss = stringify_nfcd(_cd, existing.get_value<nfcd_loc>());

                    ABORT_F(
                        "Key '%s' is non-object in one argument and object in another. Existing value = %s",
                        ret.key.c_str(),
                        fo_ss::c_str(ss));
                }

                child_object = existing.get_value<TempObject *>();
            }

            fo::set(ret.last_containing_object->m, ret.key, TempValue(child_object));
            ret.last_containing_object = child_object;

            scanner::next(_sc);

            if (_sc.current_tok != scanner::IDENT) {
                error_message("Expected an identifer in nested name after '.'", true);
            }

            char *str = scanner::token_text(_sc, ta);
            ret.key = str;
        }

        return ret;
    }

    nfcd_loc add_number() {
        if ((long)IntMaxMin<i32>::min > _sc.current_int || _sc.current_int > (long)IntMaxMin<i32>::max) {
            error_message(fmt::format("Cannot represent integer - {} in signed 32-bit", _sc.current_int),
                          true);
        }

        return nfcd_add_number(&_cd, (double)_sc.current_int);
    };

    TempValue parse_value() {
        switch (_sc.current_tok) {
        case scanner::INT: {
            nfcd_loc loc = add_number();
            scanner::next(_sc);
            return TempValue(loc);

        } break;

        case scanner::FLOAT: {
            nfcd_loc loc = nfcd_add_number(&_cd, _sc.current_float);
            scanner::next(_sc);
            return TempValue(loc);
        } break;

        case '[': {
            scanner::next(_sc);

            int index = 0;

            std::vector<nfcd_loc> numbers;

            while (_sc.current_tok != ']') {
                if (_sc.current_tok == scanner::INVALID || scanner::EOFS) {
                    error_message(fmt::format("Expected closing ']' for array found - {}",
                                              scanner::desc(_sc.current_tok)),
                                  true);
                }

                if (_sc.current_tok != scanner::INT || _sc.current_tok != scanner::FLOAT) {
                    error_message(
                        fmt::format("Expected a number while parsing {}-th element of array", index), true);
                }

                numbers.push_back(add_number());
                scanner::next(_sc);
            }

            scanner::next(_sc);

            nfcd_loc array = nfcd_add_array(&_cd, (int)numbers.size());
            for (auto n : numbers) {
                nfcd_push(&_cd, array, n);
            }
            return TempValue(array);

        } break;

        case scanner::STRING: {
            fo::TempAllocator512 ta;
            fo_ss::Buffer string(ta);
            scanner::token_text(_sc, string);
            fo_ss::Buffer unescaped(ta);
            scanner::string_token(unescaped, string);
            return TempValue(nfcd_add_string(&_cd, fo_ss::c_str(unescaped)));

        } break;

        default: {
            error_message("Unexpected value", true);
        }
        }
        return nfcd_null();
    }
};

Error Storage::init_from_args(int ac, char **av, jsonvalidate::Validator *validator) {
    _cd = nfcd_make(simple_nf_realloc, nullptr, 0, 0);

    nfcd_loc root = nfcd_add_object(&_cd, ac);
    nfcd_set_root(_cd, root);

    ArgumentParser parser(ac, av);
    parser.parse(&_cd);

    if (validator) {
        if (!validator->validate(_cd, nfcd_root(_cd), false)) {
            return Error("Failed to validate arguments");
        }
    }

    return Error::ok();
}

} // namespace inistorage

TU_LOCAL void recurse_object(nfcd_ConfigData *cd, fo::string_stream::Buffer &ss, nfcd_loc loc) {
    using namespace fo::string_stream;
    switch (nfcd_type(cd, loc)) {
    case NFCD_TYPE_NULL:
        ss << "null";
        break;

    case NFCD_TYPE_TRUE:
        ss << "true";
        break;

    case NFCD_TYPE_FALSE:
        ss << "false";
        break;

    case NFCD_TYPE_STRING: {
        const char *s = nfcd_to_string(cd, loc);
        ss << '"' << s << '"';
    } break;

    case NFCD_TYPE_NUMBER:
        ss << nfcd_to_number(cd, loc);
        break;

    case NFCD_TYPE_ARRAY: {
        ss << "[";
        int len = nfcd_array_size(cd, loc);
        for (int i = 0; i < len; ++i) {
            nfcd_loc element = nfcd_array_item(cd, loc, i);
            recurse_object(cd, ss, element);
            ss << ",";
        }

        ss << "]";
    } break;

    case NFCD_TYPE_OBJECT: {
        ss << "{";

        int len = nfcd_object_size(cd, loc);

        for (int i = 0; i < len; ++i) {
            nfcd_ObjectItem *item = nfcd_object_item(cd, loc, i);
            ss << "\"" << nfcd_to_string(cd, item->key) << "\":";
            recurse_object(cd, ss, item->value);
            ss << ",";
        }

        ss << "}";
    } break;

    default:
        ABORT_F("Invalid NFCD type '%i'", nfcd_type(cd, loc));
    }
};

fo::string_stream::Buffer
stringify_nfcd(nfcd_ConfigData *cd, nfcd_loc root_object, fo::string_stream::Buffer ss) {
    using namespace fo::string_stream;

    root_object = nfcd_type(cd, root_object) == NFCD_TYPE_NULL ? nfcd_root(cd) : root_object;
    recurse_object(cd, ss, root_object);

    return ss;
}

