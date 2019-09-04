// Working with simple SJSON files and even simpler command line options. Because most command line
// libraries... suck.

#pragma once

// extern "C" {
#include "nflibs.h"
// }

#include <learnogl/error.h>
#include <learnogl/essential_headers.h>

#include <loguru.hpp>
#include <scaffold/debug.h>
#include <scaffold/string_stream.h>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// A simple memory allocation function that simply forwards the bare essential arguments to realloc to use for
// the nf_* data structures. So yes, no memory tracking for these data structures as of now. We must not
// forget to free the nf_* data structures!
extern "C" void *simple_nf_realloc(
    void *user_data, void *prev_pointer, int orig_size, int new_size, const char *file, int line);

// An sjson format I will use is simply to have a file consisting of key value pairs, with equal signs instead
// of colons, and no commas between array or object members. This function initializes a settings representing
// that
static inline nfjp_Settings simple_nfjson() {
    return nfjp_Settings{
        true, // unquoted_keys
        true, // c_comments
        true, // implicit_root_object
        true, // optional_commas
        true, // equals_for_colon;
        true  // python_multiline_strings;
    };
}

// Parses the given simple json string (null terminated).
nfcd_ConfigData *simple_parse_cstr(const char *src, bool abort_on_error);

// Parses data from the file. Aborts on error.
nfcd_ConfigData *simple_parse_file(const char *file_path, bool abort_on_error);

nfcd_loc simple_get_qualified(nfcd_ConfigData *cd, const char *qualified_name);

// Returns a pointer to the cstring at given location.
const char *simple_to_string(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error);

inline nfcd_loc _simple_must(nfcd_loc loc, const char *file, int line) {
    CHECK_F(loc != nfcd_null(), "LOC is null, FILE: %s, LINE: %i", file, line);
    return loc;
}

#define SIMPLE_MUST(loc) _simple_must(loc, __FILE__, __LINE__)

inline constexpr const char *str_nfcd_type(int type) {
    switch (type) {
    case NFCD_TYPE_NULL:
        return "NFCD_TYPE_NULL";
    case NFCD_TYPE_FALSE:
        return "NFCD_TYPE_FALSE";
    case NFCD_TYPE_TRUE:
        return "NFCD_TYPE_TRUE";
    case NFCD_TYPE_NUMBER:
        return "NFCD_TYPE_NUMBER";
    case NFCD_TYPE_STRING:
        return "NFCD_TYPE_STRING";
    case NFCD_TYPE_ARRAY:
        return "NFCD_TYPE_ARRAY";
    case NFCD_TYPE_OBJECT:
        return "NFCD_TYPE_OBJECT";
    default:
        return "BAD NFCD_TYPE";
    };
}

// Key-value kind of file parsing
namespace inistorage {

using Variant = VariantTable<std::string, double, bool, fo::Vector2, fo::Vector3, fo::Vector4>;

// This class is meant for conveniently reading in human-written configurations. Can have object inside object
// (nested objects), but arrays should be vectors 1, 2, 3 only. Cannot have array of objects, arrays, strings,
// or bools. This is not a good choice for parsing a document that is mostly a large array. Humans don't write
// large arrays usually.
struct Storage {
    std::unordered_map<std::string, Variant> _map;

    nfcd_ConfigData *_cd = nullptr;

    bool _initialized = false;

    Storage() = default;

    // Constructor
    Storage(const fs::path &path, bool keep_config_data = true);

    // Initialize a storage object from a given file
    void init_from_file(const fs::path &path, bool keep_config_data = true);

    // Initialize a storage object from command line arguments
    const char *init_from_args(int ac, char **av);

    nfcd_ConfigData *cd() const { return _cd; }

    bool is_empty() const { return _map.size() == 0; }

    template <typename ValueType>::optional<ValueType> _get_maybe(const char *key) {
        auto iter = _map.find(key);
        if (iter == _map.end()) {
            return ::nullopt;
        }

        if (type_index(iter->second) != Variant::index<ValueType>) {
            return ::nullopt;
        }
        return get_value<ValueType>(iter->second);
    }

    // All these methods accept the key and a refernce to the output variable where the value will be assigned
    // to. Also takes an optional default value. If key doesn't exist, and you provide a default value, the
    // default value will be assigned. In either case the returned boolean indicates whether key existed in
    // table or not.

    bool string(const char *key, std::string &out, ::optional<std::string> default_value = ::nullopt) {
        auto res = _get_maybe<std::string>(key);
        if (res) {
            out = std::move(res.value());
            return true;
        } else if (default_value) {
            out = std::move(default_value.value());
        }
        return false;
    }

    template <typename T> bool number(const char *key, T &out, ::optional<T> default_value = ::nullopt) {
        static_assert(std::is_arithmetic<T>::value, "");
        auto res = _get_maybe<double>(key);
        if (res) {
            out = static_cast<T>(res.value());
            return true;
        } else if (default_value) {
            out = static_cast<T>(default_value.value());
        }

        return false;
    }

    template <typename T> bool boolean(const char *key, T &out, ::optional<T> default_value = ::nullopt) {
        static_assert(std::is_integral<T>::value || std::is_same<T, bool>::value, "");
        auto res = _get_maybe<bool>(key);
        if (res) {
            out = static_cast<T>(res.value());
            return true;
        } else if (default_value) {
            out = static_cast<T>(default_value.value());
        }

        return false;
    }

    bool vector2(const char *key, fo::Vector2 &out, ::optional<fo::Vector2> default_value = ::nullopt) {
        auto res = _get_maybe<fo::Vector2>(key);
        if (res) {
            out = res.value();
            return true;
        } else if (default_value) {
            out = default_value.value();
        }
        return false;
    }

    bool vector3(const char *key, fo::Vector3 &out, ::optional<fo::Vector3> default_value = ::nullopt) {
        auto res = _get_maybe<fo::Vector3>(key);
        if (res) {
            out = res.value();
            return true;
        } else if (default_value) {
            out = default_value.value();
        }
        return false;
    }

    bool vector4(const char *key, fo::Vector4 &out, ::optional<fo::Vector4> default_value = ::nullopt) {
        auto res = _get_maybe<fo::Vector4>(key);
        if (res) {
            out = res.value();
            return true;
        } else if (default_value) {
            out = default_value.value();
        }
        return false;
    }
};

// Macro to store a default value into given variable if the key doesn't exist in the ini.
#define INI_STORE_DEFAULT(key, method_call, variable_to_store_in, default_value)                             \
    if (!method_call(key, variable_to_store_in)) {                                                           \
        variable_to_store_in = default_value;                                                                \
    }

} // namespace inistorage

// nfcd_ConfigData to JSON string.
fo::string_stream::Buffer stringify_nfcd(nfcd_ConfigData *cd, fo::string_stream::Buffer ss = {});

// Functions for parsing some frequently used data types

template <typename T> struct SimpleParse;

template <> struct SimpleParse<float> {
    static float parse(nfcd_ConfigData *cd, nfcd_loc loc) {
        CHECK_F(nfcd_type(cd, loc) == NFCD_TYPE_NUMBER, "Wrong type: %i", nfcd_type(cd, loc));
        double f = nfcd_to_number(cd, loc);
        return (float)f;
    }
};

template <> struct SimpleParse<fo::Vector2> {
    static fo::Vector2 parse(nfcd_ConfigData *cd, nfcd_loc loc) {
        CHECK_F(nfcd_type(cd, loc) == NFCD_TYPE_ARRAY, "Wrong type: %i", nfcd_type(cd, loc));
        CHECK_F(nfcd_array_size(cd, loc) == 2, "");
        fo::Vector2 v;
        for (int i = 0; i < 2; ++i) {
            v[i] = SimpleParse<float>::parse(cd, nfcd_array_item(cd, loc, i));
        }
        return v;
    }
};

template <> struct SimpleParse<fo::Vector3> {
    static fo::Vector3 parse(nfcd_ConfigData *cd, nfcd_loc loc) {
        CHECK_F(nfcd_type(cd, loc) == NFCD_TYPE_ARRAY, "Wrong type: %i", nfcd_type(cd, loc));
        CHECK_F(nfcd_array_size(cd, loc) == 3, "");
        fo::Vector3 v;
        for (int i = 0; i < 3; ++i) {
            v[i] = SimpleParse<float>::parse(cd, nfcd_array_item(cd, loc, i));
        }
        return v;
    }
};

template <> struct SimpleParse<fo::Vector4> {
    static fo::Vector4 parse(nfcd_ConfigData *cd, nfcd_loc loc) {
        CHECK_F(nfcd_type(cd, loc) == NFCD_TYPE_ARRAY, "Wrong type: %i", nfcd_type(cd, loc));
        CHECK_F(nfcd_array_size(cd, loc) == 4, "");
        fo::Vector4 v;
        for (int i = 0; i < 4; ++i) {
            v[i] = SimpleParse<float>::parse(cd, nfcd_array_item(cd, loc, i));
        }
        return v;
    }
};

template <typename T> struct SimpleParse<std::vector<T>> {
    static std::vector<T> parse(nfcd_ConfigData *cd, nfcd_loc loc) {
        CHECK_F(nfcd_type(cd, loc) == NFCD_TYPE_ARRAY, "Wrong type: %i", nfcd_type(cd, loc));

        std::vector<T> vec;

        int size = nfcd_array_size(cd, loc);
        if (size == 0)
            return vec;

        vec.reserve((size_t)size);

        for (int i = 0; i < size; ++i) {
            vec.push_back(SimpleParse<T>::parse(cd, nfcd_array_item(cd, loc, i)));
        }
        return vec;
    }
};

// Just a function to replace the project-relative path to an absolute path
fs::path simple_absolute_path(const char *path_relative_to_project);

namespace jsonvalidate {
struct Validator {
    std::string desc = "No desc";

    virtual ~Validator(){};

    void set_desc(std::string desc) { this->desc = std::move(desc); }

    virtual bool validate(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) = 0;
};

struct Object : Validator {
    fo::OrderedMap<std::string, Validator *> items;

    ~Object();

    Validator &add_item(const std::string &key, Validator *validator) {
        fo::set(items, key, validator);
        return *this;
    }

    virtual bool validate(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) {
        for (auto &e : items) {
            nfcd_loc value = nfcd_object_lookup(cd, loc, e.first().c_str());
            if (nfcd_type(cd, value) == NFCD_TYPE_NULL) {
                LOG_F(ERROR, "Key '%s' not found in object (%s)", e.first().c_str(), desc.c_str());
                if (abort_on_error) {
                    ABORT_F("See log");
                }

                return false;
            }

            bool ok = e.second()->validate(cd, value, false);
            if (!ok) {
                LOG_F(ERROR,
                      "Value of key '%s' in object (%s) does not validate",
                      e.first().c_str(),
                      desc.c_str());

                if (abort_on_error) {
                    ABORT_F("See log");
                }
            }
        }
    }
};

struct Array : Validator {
    struct IfList {
        u32 count = 0;
        Validator *validator = nullptr;
    };

    using SeparateTypes = fo::Array<Validator *>;

    ::VariantTable<SeparateTypes, IfList> data = SeparateTypes{};

    ~Array() {}

    Validator &add_item(Validator *validator) {
        CHECK_F(!data.contains_subtype<IfList>(), "Already specified as list");
        auto &array = data.get_value<SeparateTypes>();
        fo::push_back(array, validator);
        return *this;
    }

    Validator &make_list(Validator *validator, u32 count) {
        data = IfList{ count, validator };
        return *this;
    }

    u32 _expected_count() const {
        if (data.contains_subtype<IfList>()) {
            return data.get_value<IfList>().count;
        }
        return fo::size(data.get_value<SeparateTypes>());
    }

    Validator *_get_validator(u32 i) const {
        if (data.contains_subtype<IfList>()) {
            return data.get_value<IfList>().validator;
        }
        return data.get_value<SeparateTypes>()[i];
    }

    virtual bool validate(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) {
        if (nfcd_type(cd, loc) != NFCD_TYPE_ARRAY) {
            LOG_F(ERROR,
                  "Expected an array (%s) but found type %s",
                  desc.c_str(),
                  str_nfcd_type(nfcd_type(cd, loc)));

            if (abort_on_error) {
                ABORT_F("See log");
            }
        }

        int count = nfcd_array_size(cd, loc);

        CHECK_EQ_F(count, _expected_count(), "Expected count = %u", count);

        for (u32 i = 0; i < (u32)count; ++i) {
            auto validator = _get_validator(i);
            bool ok = validator->validate(cd, nfcd_array_item(cd, loc, i), false);

            if (!ok) {
                LOG_F(ERROR, "Item %u of array (%s) failed to validate", i, desc.c_str());
                if (abort_on_error) {
                    ABORT_F("See log");
                }
                return false;
            }
        }

        return true;
    }
};

struct Number : Validator {
    ~Number();

    virtual bool validate(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) {
        auto t = nfcd_type(cd, loc);

        if (t != NFCD_TYPE_NUMBER) {
            LOG_F(ERROR, "Expected a number for (%s). Found %s", desc.c_str(), str_nfcd_type(t));

            if (abort_on_error) {
                ABORT_F("See log");
            }
            return false;
        }

        return true;
    }
};

struct String : Validator {
    ~String();

    virtual bool validate(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) {
        auto t = nfcd_type(cd, loc);

        if (t != NFCD_TYPE_STRING) {
            LOG_F(ERROR, "Expected a string for (%s). Found %s", desc.c_str(), str_nfcd_type(t));

            if (abort_on_error) {
                ABORT_F("See log");
            }
            return false;
        }

        return true;
    }
};

struct Boolean : Validator {
    ~Boolean();

    virtual bool validate(nfcd_ConfigData *cd, nfcd_loc loc, bool abort_on_error) {
        auto t = nfcd_type(cd, loc);

        if (t != NFCD_TYPE_TRUE && t != NFCD_TYPE_FALSE) {
            LOG_F(ERROR, "Expected a boolean for (%s). Found %s", desc.c_str(), str_nfcd_type(t));

            if (abort_on_error) {
                ABORT_F("See log");
            }
            return false;
        }

        return true;
    }
};

} // namespace jsonvalidate

// Validates given json with given spec. The spec has the following format as you can see from the example -
DONT_KEEP_INLINED void validate_json(jsonvalidate::Validator *validator, nfcd_ConfigData *input_json_config) {
    validator->validate(input_json_config, nfcd_root(input_json_config), true);
}
