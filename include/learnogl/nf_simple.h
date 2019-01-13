// This file defines some convenience routines while working with the nf_* data structures. I am prefixing
// each function name with `simple_`. Really bad naming.

#pragma once

// extern "C" {
#include "nflibs.h"
// }

#include <learnogl/essential_headers.h>

#include <loguru.hpp>
#include <scaffold/debug.h>
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

// INI file parsing
namespace inistorage {

using Variant = VariantTable<std::string, double, bool, fo::Vector2, fo::Vector3, fo::Vector4>;

// Often I just use .ini-ish files. So this is for that
struct Storage {
    std::unordered_map<std::string, Variant> _map;

    nfcd_ConfigData *_cd = nullptr;

    bool _initialized = false;

    Storage() = default;

    // Constructor
    Storage(const fs::path &path, bool keep_config_data = true);

    void init_from_file(const fs::path &path, bool keep_config_data = true);

    nfcd_ConfigData *cd() const { return _cd; }

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

    bool string(const char *key, std::string &out, ::optional<std::string> default_value) {
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

namespace json_schema {

// Validating JSON documents using a schema. The scheme is given as a compile time template expression. Not
// supporting arbitrary maps. Just structs, arrays with all elements having same type, strings, and numbers.

/*  ------- Example usage --------

I have a struct which laid out like this

    struct Id {
        const char *name;
        int32_t uid;
        std::vector<float> high_scores;
    };

using IdValidator = StructValidator<StringValidator, SignedIntValidator, ArrayValidator<FloatValidator>>;

IdValidator::validate(R"(
    {
        name: "rksht",
        uid: 0xdeadbeef,
        high_scores: [9.1, 9.3, 8.0, 8.5, 7.5]
    }
)");

Doing this places a restriction. For JSON objects that need to parsed as structs in C++, the fields should
appear in the same order in the JSON source. Also, all fields must be present. This is not really a huge
restriction. Allowing different can be done, but that is just a lot more chore.

*/

constexpr size_t UNBOUND_ARRAY_COUNT = std::numeric_limits<size_t>::max();

REALLY_INLINE void repeat_chars(FILE *f, int count, char c) {
    for (int i = 0; i < count; ++i) {
        fprintf(f, "%c", c);
    }
}

template <typename ElementValidator, size_t count = UNBOUND_ARRAY_COUNT> struct ArrayValidator {
    static bool validate(nfcd_ConfigData *cd, nfcd_loc loc, int value_depth = 1) {
        if (nfcd_type(cd, loc) != NFCD_TYPE_ARRAY) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr,
                    "Array validator failed - Not an array - type is %s\n",
                    str_nfcd_type(nfcd_type(cd, loc)));
            return false;
        }

        size_t len = nfcd_array_size(cd, loc);
        if (count != UNBOUND_ARRAY_COUNT && len != count) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr,
                    "Array validator failed - Count of %zu instead of given fixed count %zu\n",
                    len,
                    count);
            return false;
        }

        for (size_t i = 0; i < len; ++i) {
            nfcd_loc item_loc = nfcd_array_item(cd, loc, i);
            bool element_ok = ElementValidator::validate(cd, item_loc, value_depth + 1);
            if (!element_ok) {
                repeat_chars(stderr, value_depth, ' ');
                fprintf(stderr, "Array validator failed - Element validator failed for element - %zu\n", i);
                return false;
            }
        }

        return true;
    }
};

struct StringValidator {
    static bool validate(nfcd_ConfigData *cd, nfcd_loc loc, int value_depth = 1) {
        if (nfcd_type(cd, loc) != NFCD_TYPE_STRING) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr, "StringValidator failed - Type is %s\n", str_nfcd_type(nfcd_type(cd, loc)));
            return false;
        }
        return true;
    }
};

struct DoubleValidator {
    static bool validate(nfcd_ConfigData *cd, nfcd_loc loc, int value_depth = 1) {
        if (nfcd_type(cd, loc) != NFCD_TYPE_NUMBER) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr, "DoubleValidator failed - Type is %s\n", str_nfcd_type(nfcd_type(cd, loc)));
            return false;
        }
        return true;
    }
};

struct SignedIntegerValidator {
    static bool validate(nfcd_ConfigData *cd, nfcd_loc loc, int value_depth = 1) {
        bool res = DoubleValidator::validate(cd, loc, value_depth);
        double number = nfcd_to_number(cd, loc);
        if (!res || double(int64_t(number)) != number) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr, "SignedIntegerValidator failed - Number %lf is not an integer\n", number);
            return false;
        }
        return true;
    }
};

template <typename... FieldValidators> struct StructValidator {
    using FieldValidatorsList = unpack_t<FieldValidators...>;

    static constexpr size_t num_fields = len_v<FieldValidatorsList>;

    static_assert(num_fields != 0, "Struct must have at least 1 field");

    // nfcd_json_parser places the key values sequentially as they appear in the JSON file. So using the
    // 'index' of the object item is ok here to walk each sub-item.
    template <size_t i, size_t len> struct ValidateField {
        static bool validate(nfcd_ConfigData *cd, nfcd_loc object, int value_depth = 1) {
            auto oi = nfcd_object_item(cd, object, i);

            using Validator = nth_t<FieldValidatorsList, i>;

            if (!Validator::validate(cd, oi->value, value_depth + 1)) {
                repeat_chars(stderr, value_depth, ' ');
                fprintf(stderr, "FieldValidator failed ^^^\n");
                return false;
            }

            return ValidateField<i + 1, len>::validate(cd, object, value_depth);
        }
    };

    template <size_t len> struct ValidateField<len, len> {
        static bool validate(nfcd_ConfigData *, nfcd_loc, int) { return true; }
    };

    static bool validate(nfcd_ConfigData *cd, nfcd_loc object_loc, int value_depth = 1) {
        int len = nfcd_object_size(cd, object_loc);

        if (len != num_fields) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr, "StructValidator failed - Number of fields don't match\n");
            return false;
        }

        bool res = ValidateField<0, num_fields>::validate(cd, object_loc, value_depth);
        if (!res) {
            repeat_chars(stderr, value_depth, ' ');
            fprintf(stderr, "StructValidator failed - Some field didn't validate\n");
            return false;
        }

        return true;
    }
};

} // namespace json_schema
