#pragma once

#include <duk_console.h>
#include <duktape.h>

#include <learnogl/kitchen_sink.h>
#include <scaffold/temp_allocator.h>

struct WrappedDukContext : NonCopyable {
    duk_context *_context = nullptr;
    fo::Allocator *_allocator = &fo::memory_globals::default_allocator();
    bool _destroy_on_dtor = true;
    bool _enable_console = true;

    fo::Vector<fs::path> _module_search_paths;

    void init();
    void destroy();

    duk_context *C() const { return _context; }

    bool is_initialized() const { return _context != nullptr; }

    void set_use_default_heap(bool set_use_default_heap = false);

    void set_allocator(fo::Allocator &allocator) { _allocator = &allocator; }

    void set_destroy_on_dtor(bool destroy_on_dtor) { _destroy_on_dtor = destroy_on_dtor; }

    void set_enable_console(bool enable) {
        assert(_context == nullptr);
        _enable_console = enable;
    }

    void add_module_path(fs::path path) {
        for (const auto &p : _module_search_paths) {
            if (fs::equivalent(path, p)) {
                return;
            }
        }
        fo::push_back(_module_search_paths, std::move(path));
    }

    // WrappedDukContext();
    ~WrappedDukContext();
};

const char *str_duk_type(duk_int_t type);

// Pushes the object refered to by the given variable onto the stack, if variable exists. Otherwise returns
// false.
bool get_qualified_duk_variable(duk_context *duk, const char *qualified_name);

// For arguments residing on the stack.
struct DukStackIndex {
    int _i;

    DukStackIndex(int i)
        : _i(i) {}

    explicit operator int() const { return _i; }
};

struct DukObjectByName {
    const char *_qualified_name;

    DukObjectByName(const char *qualified_name)
        : _qualified_name(qualified_name) {}
};

struct DukFnArg {
    ::VariantTable<float, const char *, DukObjectByName, DukStackIndex> _variant;

    DukFnArg(float number)
        : _variant(number) {}

    DukFnArg(const char *c_string)
        : _variant(c_string) {}

    void push(duk_context *ctx) const {
        VT_SWITCH(_variant) {
            VT_CASE(_variant, float)
                : {
                duk_push_number(ctx, get_value<float>(_variant));
            }
            break;

            VT_CASE(_variant, const char *)
                : {
                duk_push_string(ctx, get_value<const char *>(_variant));
            }
            break;

            VT_CASE(_variant, DukObjectByName)
                : {
                get_qualified_duk_variable(ctx, get_value<DukObjectByName>(_variant)._qualified_name);
            }
            break;

            VT_CASE(_variant, DukStackIndex)
                : {
                duk_push_false(ctx);
                duk_copy(ctx, get_value<DukStackIndex>(_variant)._i, duk_get_top(ctx));
            }
            break;
        }
    }
};

// Create a global duk object and store it in the variable named `global_name`.
bool create_global_duk_object(duk_context *ctx,
                              const char *global_name,
                              const char *qualified_ctor_name,
                              std::vector<DukFnArg> arguments);

// Evaluates the duktape source file.
duk_int_t eval_duk_source_file(duk_context *ctx, const fs::path &file_path);

struct CallDukMethodOnVariable {
    // Returns C++ true if there was no errors, otherwise falls. The returned duktape value is on the top of
    // stack. Optionally you can choose to pop it.
    bool CALL_OPERATOR(duk_context *ctx,
                       const char *variable_name,
                       const char *method_name,
                       const std::vector<DukFnArg> &args);

    // For functions that don't return any value (aka return an undefined value), we gonna pop it if set to
    // true.
    bool32 pop_return_value = 0;
};

struct CallDukMethod {
    fo::TempAllocator256 _args_alloc;
    fo::Vector<DukFnArg> args{ _args_alloc };

    bool pop_return_value = false;

    CallDukMethod() { fo::reserve(args, 128); }

    bool CALL_OPERATOR(duk_context *ctx, int index_of_object, const char *method_name);

    void add_arg(DukFnArg arg) { fo::push_back(args, arg); }

    void clear_args() { fo::clear(args); }
};

// Gets the value of the property `prop_name` of the global variable name `global_name`
bool get_prop_of_duk_variable(duk_context *ctx, const char *global_name, const char *prop_name);

struct DukBufferInfo {
    u32 bytes_per_element;
    u32 num_elements;
    void *ptr; // Pointer to starting bytes
};

template <typename T> struct DukTypedBufferInfo {
    u32 num_elements;
    T *ptr; // Pointer to starting bytes
};

template <typename T> DukTypedBufferInfo<T> cast_duk_buffer_info(DukBufferInfo &info) {
    DukTypedBufferInfo<T> tinfo;
    tinfo.num_elements = info.num_elements;
    tinfo.ptr = reinterpret_cast<T *>(info.ptr);

    assert(info.bytes_per_element == sizeof(T));

    return tinfo;
}

// Returns a struct containing the info and data pointer for the buffer residing at given index.
DukBufferInfo get_duk_buffer_info(duk_context *ctx, int buffer_index);
