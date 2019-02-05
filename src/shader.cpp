#include <fmt/format.h>
#include <learnogl/shader.h>

#include <shaderc/shaderc.hpp>

#include <regex>
#include <set>
#include <sstream>

using namespace fo;

using StringBuffer = fo::string_stream::Buffer;

namespace eng {

ShaderDefines &ShaderDefines::add(std::string name) {
    dirty = true;
    ValueType value{};
    value = OnlyDefine{};
    auto ret = _definitions.insert(std::make_pair(name, value));
    if (!ret.second) {
        ret.first->second = OnlyDefine{};
    }
    return *this;
}

ShaderDefines &ShaderDefines::add(std::string name, ValueType value) {
    dirty = true;
    auto ret = _definitions.insert(std::make_pair(name, value));
    if (!ret.second) {
        ret.first->second = std::move(value);
    }
    return *this;
}

ShaderDefines &ShaderDefines::remove(const std::string &name) {
    dirty = true;
    _definitions.erase(name);
    return *this;
}

ShaderDefines &ShaderDefines::clear_all() {
    std::map<std::string, ValueType> empty;
    std::swap(empty, _definitions);
    dirty = true;
    return *this;
}

ShaderDefinesRAII::~ShaderDefinesRAII() {
    if (_defs) {
        for (auto &name : _newly_added) {
            _defs->remove(name);
        }
    }
}

static std::string vector_string(const std::vector<float> &v) {
    std::stringstream ss;
    ss << "{";
    for (u32 i = 0; i < v.size(); ++i) {
        ss << v[i] << ", ";
    }
    ss << "}";
    return ss.str();
}

std::string ShaderDefines::get_string() const {
    std::stringstream s;
    if (dirty) {
        for (auto &kv : _definitions) {
            s << "#define " << kv.first << "    ";

            switch (type_index(kv.second)) {
            case ShaderDefines::ValueType::index<std::string>:
                s << "\"" << get_value<std::string>(kv.second) << "\"\n";
                break;
            case ShaderDefines::ValueType::index<int>:
                s << get_value<i32>(kv.second) << "\n";
                break;
            case ShaderDefines::ValueType::index<float>:
                s << get_value<float>(kv.second) << "\n";
                break;
            case ShaderDefines::ValueType::index<fo::Vector2>: {
                const auto &v = get_value<Vector2>(kv.second);
                s << fmt::format("vec2({:.5f}, {:.5f})", XY(v)) << "\n";
            } break;
            case ShaderDefines::ValueType::index<fo::Vector3>: {
                const auto &v = get_value<Vector3>(kv.second);
                s << fmt::format("vec3({:.5f}, {:.5f}, {:.5f})", XYZ(v)) << "\n";
                s << "\n";
            } break;
            case ShaderDefines::ValueType::index<fo::Vector4>: {
                const auto &v = get_value<Vector4>(kv.second);
                s << fmt::format("vec4({:.5f}, {:.5f}, {:.5f}, {:.5f})", XYZW(v)) << "\n";
                s << "\n";
            } break;

            case ShaderDefines::ValueType::index<std::vector<float>>: {
                const auto &v = get_value<std::vector<float>>(kv.second);
                s << vector_string(v) << "\n";
            } break;

            case ShaderDefines::ValueType::index<OnlyDefine>:
                s << "\n";
                break;
            default:
                CHECK_F(false, "Defines type index = %i", type_index(kv.second));
            }
        }
        s << "\n";
        cached_string = s.str();
        dirty = false;
    }
    return cached_string;
}

} // namespace eng

namespace {

constexpr auto LINE_DIRECTIVE_PATTERN = R"_re_(#line [0-9]+ (".+"))_re_";
constexpr auto INCLUDE_DIRECTIVE_ENABLE_PATTERN = "#extension GL_GOOGLE_include_directive : enable";

struct ShaderGlobals;

static_assert(alignof(std::string) % 4 == 0, "");

struct ShaderIncludePathStuff {
    shaderc_include_result inc_result; // Keep this field at the top
    char *source_name;
    char *content;
};

struct ShaderIncluderInterface : public shaderc::CompileOptions::IncluderInterface {
    virtual shaderc_include_result *GetInclude(const char *requested_source,
                                               shaderc_include_type type,
                                               const char *requesting_source,
                                               size_t include_depth) override;

    // Handles shaderc_include_result_release_fn callbacks.
    virtual void ReleaseInclude(shaderc_include_result *data) override;

    // Allocates file path and content
    fo::Allocator *_allocator;

    ShaderGlobals *_s = nullptr;

    ShaderIncluderInterface(ShaderGlobals *s,
                            fo::Allocator &allocator = fo::memory_globals::default_allocator())
        : CTOR_INIT_FIELD(_allocator, &allocator)
        , CTOR_INIT_FIELD(_s, s) {}

    virtual ~ShaderIncluderInterface() {
        _s = nullptr;
        _allocator = nullptr;
    }
};

// Stuff stored for shader compilation.
struct ShaderGlobals {
    std::regex re_include_directive_enable;
    std::regex re_line_directive;

    fo::Vector<fs::path> _shader_search_paths;

    GETONLY(shaderc::Compiler, sc_compiler);

    bool print_full_shader = false;
    bool print_after_preprocessing = false;
    bool abort_on_compile_error = true;

    ShaderGlobals()
        : CTOR_INIT_FIELD(re_include_directive_enable, INCLUDE_DIRECTIVE_ENABLE_PATTERN)
        , CTOR_INIT_FIELD(re_line_directive, LINE_DIRECTIVE_PATTERN)
        , CTOR_INIT_FIELD(_shader_search_paths, 0, fo::memory_globals::default_scratch_allocator()) {}
};

shaderc_include_result *ShaderIncluderInterface::GetInclude(const char *requested_source,
                                                            shaderc_include_type type,
                                                            const char *requesting_source,
                                                            size_t include_depth) {
    auto s = SELF._s;

    ShaderIncludePathStuff path_stuff = {};

    // LOG_F(INFO, "requested_source = %s", requested_source);

    fs::path requester_path(requesting_source);

    bool search_in_cur_dir = type == shaderc_include_type::shaderc_include_type_relative;

    if (!requester_path.is_absolute()) {
        LOG_F(ERROR, "Requester should always be absolute path the way I'm using this interface");
        search_in_cur_dir = false;
    }

    LOCAL_FUNC get_include = [&](const fs::path &path_to_source) {
        std::string path_str = path_to_source.generic_u8string();

        // Copy source path
        char *path_cstr = (char *)_allocator->allocate(path_str.size() + 1);

        memcpy(path_cstr, path_str.c_str(), path_str.size());
        path_cstr[path_str.size()] = '\0';

        // File contents
        path_stuff.inc_result.source_name = path_cstr;
        path_stuff.inc_result.source_name_length = path_str.size();

        u32 size = 0;
        char *content = read_file(path_to_source, *_allocator, size, false);

        path_stuff.inc_result.content = content;
        path_stuff.inc_result.content_length = size;

        CHECK_EQ_F(size, fs::file_size(path_to_source));

#if 0
            LOG_F(INFO, "Found source at - %s", path_cstr);
            LOG_F(INFO,
                  "Source content -\n%.*s",
                  (int)path_stuff.inc_result.content_length,
                  path_stuff.inc_result.content);
#endif

        path_stuff.source_name = path_cstr;
        path_stuff.content = content;

        auto p =
            fo::make_new<ShaderIncludePathStuff>(fo::memory_globals::default_scratch_allocator(), path_stuff);

#if 0
        LOG_F(INFO,
              "include result pointer = %p, source_name = %p, content = %p",
              p,
              path_stuff.source_name,
              path_stuff.content);

#endif

        p->inc_result.user_data = this;

        return &(p->inc_result);
    };

    if (search_in_cur_dir) {
        fs::path requested_path = requester_path;
        requested_path = requested_path.parent_path();
        requested_path /= requested_source;

        if (fs::exists(requested_path)) {
            return get_include(requested_path);
        }
    }

    for (auto &search_path : s->_shader_search_paths) {
        fs::path path_to_source = search_path / requested_source;

        if (fs::exists(path_to_source)) {
            return get_include(path_to_source);
        }
    }

    return nullptr;
}

void ShaderIncluderInterface::ReleaseInclude(shaderc_include_result *include_result) {
    if (include_result == nullptr) {
        return;
    }

    ShaderIncludePathStuff *path_stuff = reinterpret_cast<ShaderIncludePathStuff *>(include_result);

#if 0
    LOG_F(INFO,
          "include result pointer on return = %p, source_name = %p, content = %p",
          include_result,
          path_stuff->source_name,
          path_stuff->content);

#endif

    _allocator->deallocate(path_stuff->source_name);
    _allocator->deallocate(path_stuff->content);

    fo::memory_globals::default_scratch_allocator().deallocate(path_stuff);
}

// Add a new search path that will be looked at in `#include`
static void add_search_path(ShaderGlobals &g, fs::path path) {
    CHECK_F(path.is_absolute(), "Not an absolute path - %s", path.generic_u8string().c_str());
    CHECK_F(fs::is_directory(path), "Not a directory - %s", path.generic_u8string().c_str());
    LOG_F(INFO, "Adding shader search path - %s", path.generic_u8string().c_str());

    push_back(g._shader_search_paths, std::move(path));
}

std::aligned_storage_t<sizeof(ShaderGlobals)> shader_globals_storage[1];

ShaderGlobals &shader_globals() { return *reinterpret_cast<ShaderGlobals *>(shader_globals_storage); }

} // namespace

namespace eng {

void print_shader_info_log(GLuint shader_handle, const char *shader_name) {
    constexpr int max_length = 2048;
    int actual_length = 0;
    char log[max_length];
    glGetShaderInfoLog(shader_handle, max_length, &actual_length, log);
    {
        fo::string_stream::Buffer ss(fo::memory_globals::default_allocator());
        eng::print_callstack(ss);

        LOG_F(ERROR,
              "Error info log for shader compilation, shader = %s, (handle = %u) - \n%s\nCallstack - \n%s",
              shader_name,
              shader_handle,
              log,
              fo::string_stream::c_str(ss));
    }
}

void print_program_info_log(GLuint program_handle) {
    constexpr int max_length = 2048;
    int actual_length = 0;
    char log[max_length];

    glGetProgramInfoLog(program_handle, max_length, &actual_length, log);
    {
        fo::string_stream::Buffer ss(fo::memory_globals::default_allocator());
        eng::print_callstack(ss);

        LOG_F(ERROR,
              "Error info log for program linking (handle = %u) - \n%s\nCallstack - \n%s",
              program_handle,
              log,
              fo::string_stream::c_str(ss));
    }
}

void check_program_status(GLuint program_handle) {
    int params = -1;
    glGetProgramiv(program_handle, GL_LINK_STATUS, &params);

    if (params != GL_TRUE) {
        print_program_info_log(program_handle);
        ABORT_F("See message");
    }
}

} // namespace eng

namespace eng {

void init_shader_globals(const ShaderGlobalsConfig &config) {
    new (&shader_globals()) ShaderGlobals;

    ShaderGlobals &g = shader_globals();

    g.print_full_shader = config.print_full_shader;
    g.print_after_preprocessing = config.print_after_preprocessing;
}

void close_shader_globals() { shader_globals().~ShaderGlobals(); }

void add_shader_search_path(fs::path path) { add_search_path(shader_globals(), std::move(path)); }

void shader_compile_abort_on_error(bool v) { shader_globals().abort_on_compile_error = v; }

GLuint create_program(GLuint vert_shader, GLuint frag_shader, const char *debug_label) {
    auto program = glCreateProgram();
    glAttachShader(program, vert_shader);
    glAttachShader(program, frag_shader);
    glLinkProgram(program);

    check_program_status(program);
    set_program_label(program, debug_label);

    return program;
}

/// Same as above function, but given shaders are already compiled.
GLuint create_program(GLuint vert_shader, GLuint geom_shader, GLuint frag_shader, const char *debug_label) {
    auto program = glCreateProgram();
    glAttachShader(program, vert_shader);
    glAttachShader(program, geom_shader);
    glAttachShader(program, frag_shader);
    glLinkProgram(program);
    check_program_status(program);
    set_program_label(program, debug_label);
    return program;
}

GLuint create_compute_program(GLuint comp_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, comp_shader);
    glLinkProgram(program);
    check_program_status(program);
    return program;
}

static void add_defs_to_sc_opts(const ShaderDefines &macro_defs, shaderc::CompileOptions &sc_options) {
    // Add macro definitions from ShaderDefs to options
    for (auto &kv : macro_defs._definitions) {

        VT_SWITCH(kv.second) {
            VT_CASE(kv.second, std::string)
                : {
                sc_options.AddMacroDefinition(kv.first, get_value<std::string>(kv.second));
            }
            break;
            VT_CASE(kv.second, int)
                : {
                sc_options.AddMacroDefinition(kv.first, std::to_string(get_value<i32>(kv.second)));
            }
            break;
            VT_CASE(kv.second, float)
                : {
                sc_options.AddMacroDefinition(kv.first, std::to_string(get_value<float>(kv.second)));
            }
            break;
            VT_CASE(kv.second, fo::Vector2)
                : {
                const auto &v = get_value<Vector2>(kv.second);
                sc_options.AddMacroDefinition(kv.first, fmt::format("vec2({:.5f}, {:.5f})", XY(v)));
            }
            break;
            VT_CASE(kv.second, fo::Vector3)
                : {
                const auto &v = get_value<Vector3>(kv.second);
                sc_options.AddMacroDefinition(kv.first, fmt::format("vec3({:.5f}, {:.5f}, {:.5f})", XYZ(v)));
            }
            break;
            VT_CASE(kv.second, fo::Vector4)
                : {
                const auto &v = get_value<Vector4>(kv.second);
                sc_options.AddMacroDefinition(kv.first,
                                              fmt::format("vec4({:.5f}, {:.5f}, {:.5f}, {:.5f})", XYZW(v)));
            }
            break;
            VT_CASE(kv.second, std::vector<float>)
                : {
                const auto &v = get_value<std::vector<float>>(kv.second);
                sc_options.AddMacroDefinition(kv.first, vector_string(v));
            }
            break;

            VT_CASE(kv.second, ShaderDefines::OnlyDefine)
                : {
                sc_options.AddMacroDefinition(kv.first);
            }
            break;
        default:
            CHECK_F(false, "Defines type index = %i", type_index(kv.second));
        }
    }
}

constexpr inline shaderc_shader_kind get_sc_shader_kind(ShaderKind shader_kind) {
    switch (shader_kind) {
    case VERTEX_SHADER:
        return shaderc_shader_kind::shaderc_glsl_vertex_shader;
    case GEOMETRY_SHADER:
        return shaderc_shader_kind::shaderc_glsl_geometry_shader;
    case TESS_EVAL_SHADER:
        return shaderc_shader_kind::shaderc_glsl_tess_evaluation_shader;
    case TESS_CONTROL_SHADER:
        return shaderc_shader_kind::shaderc_glsl_tess_control_shader;
    case FRAGMENT_SHADER:
        return shaderc_shader_kind::shaderc_glsl_fragment_shader;
    case COMPUTE_SHADER:
        return shaderc_shader_kind::shaderc_glsl_compute_shader;
    default:
        return DEFAULT(shaderc_shader_kind);
    }
}

constexpr inline GLenum get_gl_shader_kind(ShaderKind shader_kind) {
    switch (shader_kind) {
    case VERTEX_SHADER:
        return GL_VERTEX_SHADER;
    case GEOMETRY_SHADER:
        return GL_GEOMETRY_SHADER;
    case TESS_EVAL_SHADER:
        return GL_TESS_EVALUATION_SHADER;
    case TESS_CONTROL_SHADER:
        return GL_TESS_CONTROL_SHADER;
    case FRAGMENT_SHADER:
        return GL_FRAGMENT_SHADER;
    case COMPUTE_SHADER:
        return GL_COMPUTE_SHADER;
    default:
        return GL_NONE;
    }
}

constexpr inline const char *shader_kind_ext(ShaderKind shader_kind) {
    switch (shader_kind) {
    case VERTEX_SHADER:
        return "vert";
    case GEOMETRY_SHADER:
        return "geom";
    case TESS_EVAL_SHADER:
        return "tesse";
    case TESS_CONTROL_SHADER:
        return "tessc";
    case FRAGMENT_SHADER:
        return "frag";
    case COMPUTE_SHADER:
        return "comp";
    default:
        return "glsl";
    }
}

GLuint compile_preprocessed_shader(const char *shader_cstr, ShaderKind shader_kind, const char *shader_name) {
    GLuint shader_handle = glCreateShader(get_gl_shader_kind(shader_kind));
    glShaderSource(shader_handle, 1, &shader_cstr, nullptr);

    glCompileShader(shader_handle);
    {
        int p = -1;
        glGetShaderiv(shader_handle, GL_COMPILE_STATUS, &p);
        if (p == GL_TRUE) {
            return shader_handle;
        }
        print_shader_info_log(shader_handle, shader_name);
        ABORT_F("See message");
    }

    return 0;
}

static void remove_unwanted_directives(std::string &pp_string) {
    LOCAL_FUNC remove_match_region = [&](std::smatch &m, int group) {
        size_t start = m.position(group);
        size_t end = m.position(group) + m.length(group);
        std::fill(pp_string.begin() + start, pp_string.begin() + end, ' ');
    };

    const auto &s = shader_globals();

    {
        std::sregex_iterator begin(pp_string.cbegin(), pp_string.cend(), s.re_include_directive_enable);
        std::sregex_iterator end;

        for (auto i = begin; i != end; ++i) {
            std::smatch m = *i;
            remove_match_region(m, 0);
        }
    }

    {
        std::sregex_iterator begin(pp_string.cbegin(), pp_string.cend(), s.re_line_directive);
        std::sregex_iterator end;

        for (auto i = begin; i != end; ++i) {
            std::smatch m = *i;
            remove_match_region(m, 1);
        }
    }
}

GLuint create_shader_object(ShaderSourceType shader_source,
                            ShaderKind shader_kind,
                            const ShaderDefines &macro_defs,
                            const char *debug_label) {

    auto &s = shader_globals();

    // Set the options to indicate OpenGL env
    shaderc::CompileOptions sc_options;
    sc_options.SetTargetEnvironment(shaderc_target_env_opengl, 0);

    // SetIncluderInterface impl
    auto includer = std::make_unique<ShaderIncluderInterface>(&s);
    sc_options.SetIncluder(std::move(includer));

    // Add macro definitions
    add_defs_to_sc_opts(macro_defs, sc_options);

#if 0

    CHECK_NE_F(type_index(shader_source),
               ShaderSourceType::index<const char *>,
               "Not supporting strings as source as of now. Use files.");

#endif

    // Create a temporary file if it's an in-memory string.
    if (type_index(shader_source) == ShaderSourceType::index<const char *>) {
        using namespace std::chrono;

        fs::path temp_path = fs::temp_directory_path();

        using u64_dura = duration<u64, std::nano>;
        u64 epoch_time = duration_cast<u64_dura>(high_resolution_clock::now().time_since_epoch()).count();

        temp_path /= fmt::format("temp_{}.{}", epoch_time, shader_kind_ext(shader_kind));

        const char *text = get_value<const char *>(shader_source);
        write_file(temp_path, reinterpret_cast<const u8 *>(text), strlen(text));

        shader_source = temp_path;
    }

    const fs::path &path = get_value<fs::path>(shader_source);
    const std::string source_path_str = path.generic_u8string();

    fo::Array<char> source_text(fo::memory_globals::default_allocator());
    ::read_file(path, source_text, false);

#if 1
    shaderc::PreprocessedSourceCompilationResult pp_result =
        s.sc_compiler().PreprocessGlsl(fo::data(source_text),
                                       fo::size(source_text),
                                       get_sc_shader_kind(shader_kind),
                                       source_path_str.c_str(),
                                       sc_options);

#else

    shaderc::Compiler compiler;
    shaderc::PreprocessedSourceCompilationResult pp_result =
        compiler.PreprocessGlsl(fo::data(source_text),
                                fo::size(source_text),
                                get_sc_shader_kind(shader_kind),
                                source_path_str.c_str(),
                                sc_options);
#endif

    if (pp_result.GetCompilationStatus() != shaderc_compilation_status_success) {
        LOG_F(ERROR, "%s", pp_result.GetErrorMessage().c_str());
        if (s.abort_on_compile_error) {
            CHECK_F(!s.abort_on_compile_error, "Preprocessing error, see the message");
        }
        return 0;
    }

    std::string preprocessed_string(pp_result.cbegin(), pp_result.cend());

    if (s.print_after_preprocessing) {
        LOG_F(INFO,
              "----------------- Preprocessed shader source --------------------\n%s\n",
              preprocessed_string.c_str());
        printf("End of Preprocessed source\n");
    }

    // The GLSL compiler for my card doesn't seem to like the quoted string after a #line directive. Manually
    // removing these (setting to empty space).
    remove_unwanted_directives(preprocessed_string);

    GLuint shader_handle =
        compile_preprocessed_shader(preprocessed_string.c_str(), shader_kind, path.u8string().c_str());

    if (shader_handle == 0 && s.abort_on_compile_error) {
        CHECK_F(false, "See message");
    }

    return shader_handle;
}

GLuint create_vsfs_shader_object(ShaderSourceType shader_source,
                                 ShaderKind shader_kind,
                                 ShaderDefines &macro_defs,
                                 const char *debug_label) {
    LOCAL_FUNC remove_and_add = [&](const char *macro_to_remove, const char *macro_to_add) {
        FindWithEnd f = find_with_end(macro_defs._definitions, std::string(macro_to_remove));
        if (f.found()) {
            macro_defs._definitions.erase(f.res_it);
        }
        macro_defs.add(macro_to_add);
    };

    switch (shader_kind) {
    case VERTEX_SHADER: {
        remove_and_add("DO_FS", "DO_VS");
    } break;

    case FRAGMENT_SHADER: {
        remove_and_add("DO_VS", "DO_FS");
    } break;

    default: { ABORT_F("Shader type given = %i", (int)shader_kind); }
    }

    return create_shader_object(shader_source, shader_kind, macro_defs, debug_label);
}

fo::string_stream::Buffer shader_short_string(const ShaderSourceType &source, int length) {
    using namespace fo::string_stream;
    Buffer ss(fo::memory_globals::default_scratch_allocator());
    fo::reserve(ss, length);

    VT_SWITCH(source) {
        VT_CASE(source, const char *)
            : {
            auto s = get_value<const char *>(source);
            int actual_length = (int)strlen(s);
            if (actual_length <= length) {
                printf(ss, "%s", s);
            } else {
                for (int i = 0; i < length / 2; ++i) {
                    ss << s[i];
                }
                ss << "....";
                for (int i = actual_length - length / 2; i < length; ++i) {
                    ss << s[i];
                }
            }
        }
        break;
        VT_CASE(source, fs::path)
            : {
            auto &s = get_value<fs::path>(source);
            ss << s.generic_u8string().c_str();
        }
        break;
    }
    return ss;
}

void UniformVariablesMap::_set_program_uniform(const ValueAndLocation &u) {
    VT_SWITCH(u.variant) {
        VT_CASE(u.variant, i32)
            : {
            glProgramUniform1i(_program, u.location, get_value<i32>(u.variant));
        }
        break;

        VT_CASE(u.variant, f32)
            : {
            glProgramUniform1f(_program, u.location, get_value<f32>(u.variant));
        }

        break;

        VT_CASE(u.variant, fo::Vector2)
            : {
            auto &v = get_value<fo::Vector2>(u.variant);
            glProgramUniform2f(_program, u.location, XY(v));
        }
        break;

        VT_CASE(u.variant, fo::Vector3)
            : {
            auto &v = get_value<fo::Vector3>(u.variant);
            glProgramUniform3f(_program, u.location, XYZ(v));
        }
        break;

        VT_CASE(u.variant, fo::Vector4)
            : {
            auto &v = get_value<fo::Vector4>(u.variant);
            glProgramUniform4f(_program, u.location, XYZW(v));
        }
        break;

        VT_CASE(u.variant, fo::IVector2)
            : {
            auto &v = get_value<fo::IVector2>(u.variant);
            glProgramUniform2i(_program, u.location, XY(v));
        }
        break;

        VT_CASE(u.variant, fo::IVector3)
            : {
            auto &v = get_value<fo::IVector3>(u.variant);
            glProgramUniform3i(_program, u.location, XYZ(v));
        }
        break;

        VT_CASE(u.variant, fo::IVector4)
            : {
            auto &v = get_value<fo::IVector4>(u.variant);
            glProgramUniform4i(_program, u.location, XYZW(v));
        }
        break;

    default: { ABORT_F("Unreachable"); } break;
    }
}

bool UniformVariablesMap::set_uniform(const char *variable_name, const UniformVariableVariant &v) {
    auto symbol = _st->get_symbol(variable_name);

    if (symbol == StringSymbol()) {
        LOG_F(ERROR, "No variable named '%s' added to uniform map", variable_name);
        return false;
    }

    auto index = _find_pair(symbol);
    if (!index) {
        LOG_F(ERROR, "No variable named with given name - sid = '%s'", variable_name);
        return false;
    }

    auto &val_and_loc = _uniform_variable_map[index.value()].second;

    if (type_index(val_and_loc.variant) != type_index(v)) {
        LOG_F(ERROR, "Uniform type mismatch between specified type and new value's type");
        return false;
    }

    val_and_loc.variant = v;
    _set_program_uniform(ValueAndLocation{ val_and_loc });

    return true;
}

} // namespace eng
