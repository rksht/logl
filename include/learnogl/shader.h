#pragma once

#include <learnogl/essential_headers.h>

#include <learnogl/file_monitor.h>
#include <learnogl/string_table.h>
#include <learnogl/typed_gl_resources.h>
#include <scaffold/bitflags.h>
#include <scaffold/murmur_hash.h>

#include <functional>
#include <map>
#include <utility>
#include <vector>

#define VERSION_450_CORE "#version 450 core\n"
#define VERSION_430_CORE "#version 430 core\n"
#define VERSION_420_CORE "#version 420 core\n"

namespace eng {

struct ShaderGlobalsConfig {
    bool print_full_shader = false;
    bool print_after_preprocessing = false;
};

constexpr ShaderGlobalsConfig default_shader_globals_config = ShaderGlobalsConfig{};

void init_shader_globals(const ShaderGlobalsConfig &config = default_shader_globals_config);
void close_shader_globals();

void add_shader_search_path(fs::path path);

void shader_compile_abort_on_error(bool v);

// You can build up some shader macro defines using this type.
struct ShaderDefines {
    struct OnlyDefine {};

    using ValueType = VariantTable<std::string,
                                   i32,
                                   float,
                                   fo::Vector2,
                                   fo::Vector3,
                                   fo::Vector4,
                                   std::vector<float>,
                                   OnlyDefine>;

    std::map<std::string, ValueType> _definitions;

    mutable bool dirty = false;
    mutable std::string cached_string;

    ShaderDefines &add(std::string name);
    ShaderDefines &add(std::string name, ValueType value);
    ShaderDefines &remove(const std::string &name);
    ShaderDefines &clear_all();

    bool has(const std::string &name) { return _definitions.find(name) != _definitions.end(); }

    // Returns "#define <NAME> <VALUE>" one for each of the added definitions
    std::string get_string() const;
};

// Add defines using RAII. Easier to track. Does NOT track more than one value per definition.
// Simply removes the macro on destruction.
struct ShaderDefinesRAII : NonCopyable {
    ShaderDefines *_defs = nullptr;
    std::vector<std::string> _newly_added;

    ShaderDefinesRAII(ShaderDefines &shader_defines)
        : _defs(&shader_defines) {}

    void add(std::string name, ShaderDefines::ValueType v) {
        _defs->add(name, std::move(v));
        _newly_added.push_back(std::move(name));
    }

    void add(std::string name) {
        _defs->add(name);
        _newly_added.push_back(std::move(name));
    }

    std::string get_string() const { return _defs->get_string(); }

    ~ShaderDefinesRAII(); // Invalid after destruction
};

GLuint compile_preprocessed_shader(const char *shader_cstr, ShaderKind shader_kind);

GLuint create_vsfs_shader_object(ShaderSourceType shader_source,
                                 ShaderKind shader_kind,
                                 ShaderDefines &macro_defs,
                                 const char *debug_label = nullptr);

/// Same as above function, but given shaders are already compiled.
GLuint create_program(GLuint vert_shader, GLuint frag_shader, const char *debug_label = nullptr);

/// Same as above function but takes a geometry shader too.
GLuint
create_program(GLuint vert_shader, GLuint geom_shader, GLuint frag_shader, const char *debug_label = nullptr);

GLuint create_program(GLuint vs, GLuint gs, GLuint tes, GLuint tcs, GLuint fs, const char *debug_label);

/// Creates a compute shader program
GLuint create_compute_program(GLuint cs);

/// Will be moved to typed_gl_resources.cpp
void print_shader_info_log(GLuint shader_handle, const char *shader_name);
void print_program_info_log(GLuint program_handle);
void check_program_status(GLuint program_handle);

fo::string_stream::Buffer shader_short_string(const ShaderSourceType &source, int length = 40);

using UniformVariableVariant =
    ::VariantTable<i32, f32, fo::Vector2, fo::Vector3, fo::Vector4, fo::IVector2, fo::IVector3, fo::IVector4>;

struct ValueAndLocation {
    UniformVariableVariant variant;
    GLint location;
};

struct UniformVariablesMap {
    using Pair = std::pair<StringSymbol, ValueAndLocation>;
    fo::Vector<Pair> _uniform_variable_map;
    GLuint _program;
    StringTable *_st;

    UniformVariablesMap(StringTable &st, GLuint program = 0)
        : _uniform_variable_map(fo::memory_globals::default_allocator())
        , _program(program)
        , _st(&st) {}

    ::optional<u32> _find_pair(StringSymbol variable_name_symbol) {
        for (u32 i = 0; i < fo::size(_uniform_variable_map); ++i) {
            if (_uniform_variable_map[i].first == variable_name_symbol) {
                return i;
            }
        }
        return ::nullopt;
    }

    void set_program(GLuint program) {
        CHECK_EQ_F(_program, 0, "Already assigned a program - %u", program);
        _program = program;
    }

    template <typename T> UniformVariablesMap &add_variable(const char *variable_name, const T &value) {
        auto symbol = _st->to_symbol(variable_name);

        auto existing_index = _find_pair(symbol);
        if (existing_index) {
            LOG_F(WARNING, "Uniform variable '%s' already added", variable_name);
        } else {
            GLint location = glGetUniformLocation(_program, variable_name);
            fo::push_back(_uniform_variable_map, std::make_pair(symbol, ValueAndLocation{ value, location }));
        }
        return *this;
    }

    void _set_program_uniform(const ValueAndLocation &u);

    void set_all(bool program_already_in_use = false) {
        if (!program_already_in_use) {
            glUseProgram(_program);
        }

        for (auto &v : _uniform_variable_map) {
            _set_program_uniform(v.second);
        }
    }

    bool set_uniform(const char *variable_name, const UniformVariableVariant &v);
};

} // namespace eng

namespace std {
template <> struct hash<eng::ShaderSourceType> {
    std::size_t operator()(const eng::ShaderSourceType &source) {
        VT_SWITCH(source) {
            VT_CASE(source, const char *)
                : {
                auto s = get_value<const char *>(source);
                return fo::murmur_hash_64(s, strlen(s), SCAFFOLD_SEED);
            }
            break;

            VT_CASE(source, fs::path)
                : {
                auto &p = get_value<fs::path>(source);
                auto str = p.generic_u8string();
                return fo::murmur_hash_64(str.c_str(), strlen(str.c_str()), SCAFFOLD_SEED);
            }
            break;
        }
    }
};

template <> struct less<eng::ShaderSourceType> {
    bool operator()(const eng::ShaderSourceType &source1, const eng::ShaderSourceType &source2) {
        int index[2] = { (int)type_index(source1), (int)type_index(source2) };

        if (index[0] < index[1]) {
            return true;
        }

        if (index[0] == vt_index<eng::ShaderSourceType, const char *>) {
            auto &s1 = get_value<const char *>(source1);
            auto &s2 = get_value<const char *>(source2);
            return strcmp(s1, s2) < 0;
        }

        if (index[0] == vt_index<eng::ShaderSourceType, fs::path>) {
            auto &s1 = get_value<fs::path>(source1);
            auto &s2 = get_value<fs::path>(source2);
            return source1 < source2;
        }

        ABORT_F("Unreachable");
        return false;
    }
};

} // namespace std
