#include "duk_helper.h"

#include <duk_console.h>
#include <duk_module_duktape.h>
#include <learnogl/callstack.h>
#include <loguru.hpp>

const char *str_duk_type(duk_int_t type)
{
    switch (type)
    {
    case (DUK_TYPE_NONE):
        return "DUK_TYPE_NONE";
    case (DUK_TYPE_UNDEFINED):
        return "DUK_TYPE_UNDEFINED";
    case (DUK_TYPE_NULL):
        return "DUK_TYPE_NULL";
    case (DUK_TYPE_BOOLEAN):
        return "DUK_TYPE_BOOLEAN";
    case (DUK_TYPE_NUMBER):
        return "DUK_TYPE_NUMBER";
    case (DUK_TYPE_STRING):
        return "DUK_TYPE_STRING";
    case (DUK_TYPE_OBJECT):
        return "DUK_TYPE_OBJECT";
    case (DUK_TYPE_BUFFER):
        return "DUK_TYPE_BUFFER";
    case (DUK_TYPE_POINTER):
        return "DUK_TYPE_POINTER";
    case (DUK_TYPE_LIGHTFUNC):
        return "DUK_TYPE_LIGHTFUNC";

    default:
        return "Unknown type";
    }
}

TU_LOCAL WrappedDukContext *get_wrapped_context_from_global(duk_context *ctx)
{
    duk_push_global_object(ctx);              // [global]
    duk_push_literal(ctx, "wrapped_context_ptr"); // [global, "wrapped_context_ptr"]
    duk_get_prop(ctx, -2);                // [global, &wrapped_context_ptr]

    CHECK_EQ_F(duk_get_type(ctx, -1), DUK_TYPE_POINTER, "Expected pointer to WrappedDukContext");

    auto ptr = reinterpret_cast<WrappedDukContext *>(duk_get_pointer(ctx, -1));

    duk_pop_2(ctx);
    return ptr;
}

// The Duktape.modSearch function
static duk_ret_t my_mod_search(duk_context *ctx)
{
    // args are (id, require, exports, module)
    CHECK_EQ_F(duk_get_top(ctx), 4);

    std::string resolved_id = duk_safe_to_string(ctx, 0);
    resolved_id += ".js";

    LOG_F(INFO, "%s - called. module id = %s", __PRETTY_FUNCTION__, resolved_id.c_str());

    auto wrapped_context = get_wrapped_context_from_global(ctx);

    duk_ret_t ret = 0;

    for (const auto &search_path : wrapped_context->_module_search_paths)
    {
        const auto file_path = search_path / resolved_id;
        if (fs::exists(file_path))
        {
            wrapped_context->push_source_file(file_path);
            ret = 1;
        }
    }

    if (ret == 0)
    {
        LOG_F(WARNING, "Duktape.modSearch - module %s not found", resolved_id.c_str());
    }

    return ret;
}

bool get_qualified_duk_variable(duk_context *ctx, const char *qualified_name)
{
    // Quick search over the full name to see if there's any '.'. If not, just get the property from the
    // global object.
    if (strchr(qualified_name, '.') == nullptr)
    {
        duk_require_stack(ctx, 2);

        const int size_on_enter = duk_get_top(ctx);

        duk_push_global_object(ctx);

        int exists = duk_get_prop_string(ctx, size_on_enter + 0, qualified_name);

        if (!exists)
        {
            LOG_F(ERROR, "No global variable named '%s'", qualified_name);
            duk_set_top(ctx, 45);
            return false;
        }
        else
        {
            LOG_F(
              INFO,
              "Found object for qualified variable '%s' - type is - '%s', is a function? '%s'",
              qualified_name,
              str_duk_type(duk_get_type(ctx, size_on_enter + 1)),
              duk_is_function(ctx, size_on_enter + 1) ? "yes" : "no");
        }

        duk_copy(ctx, size_on_enter + 1, size_on_enter + 0);
        duk_set_top(ctx, size_on_enter + 1);
        return true;
    }

    fo::TempAllocator1024 ta;

    fo::Vector<fo_ss::Buffer> sub_names(ta);
    fo::reserve(sub_names, 4);

    const int n = (int)split_string(qualified_name, '.', sub_names, ta);

    for (int i = 0; i < n; ++i)
    {
        printf("Subname = %s\n", fo_ss::c_str(sub_names[i]));
    }

    duk_require_stack(ctx, n + 4);

    duk_push_global_object(ctx);

    for (int i = 1; i <= n; ++i)
    {
        int exists = duk_get_prop_string(ctx, -1, fo_ss::c_str(sub_names[i - 1]));
        if (!exists)
        {
            LOG_F(ERROR,
                  "Qualified variable - %s does not exist - full qualified name - %s",
                  fo_ss::c_str(sub_names[i - 1]),
                  qualified_name);
            duk_copy(ctx, -1, -(i + 1));
            duk_set_top(ctx, -(i + 1));
            return false;
        }
    }

    duk_copy(ctx, -1, -(n + 1));
    duk_set_top(ctx, -n);
    //
    return true;
}

bool create_global_duk_object(duk_context *ctx,
                  const char *global_variable_name,
                  const char *qualified_ctor_name,
                  std::vector<DukFnArg> arguments)
{
    int exists = get_qualified_duk_variable(ctx, qualified_ctor_name);

    if (!exists)
    {
        return false;
    }

    CHECK_F((bool)duk_is_function(ctx, -1),
        "Found the global but it's not a function '%s', actual type =  %s",
        qualified_ctor_name,
        str_duk_type(duk_get_type(ctx, -1)));

    // Push each argument
    for (auto &a : arguments)
    {
        a.push(ctx);
    }

    int error = duk_pnew(ctx, arguments.size());
    CHECK_EQ_F(error, 0);

    // [new_object]
    duk_push_string(ctx, global_variable_name);
    duk_push_global_object(ctx);
    // [new_object] [variable_name] [global]

    duk_swap(ctx, -1, -3);
    // [global] [variable_name] [new_object]

    duk_put_prop(ctx, -3);

    duk_pop_n(ctx, 1);
    // empty

    return true;
}

duk_int_t eval_duk_source_file(duk_context *ctx, const fs::path &file_path)
{
    fo::Array<char> text;
    read_file(file_path, text, true);
    duk_push_string(ctx, fo::data(text));
    duk_int_t result = duk_peval(ctx);

    if (result != 0)
    {
        LOG_F(ERROR, "Duk peval error - '%s'", duk_safe_to_string(ctx, -1));
    }
    else
    {
        LOG_F(INFO, "Evaluated file, returned '%s'", duk_safe_to_string(ctx, -1));
    }
    return result;
}

bool CallDukMethodOnVariable::CALL_OPERATOR(duk_context *ctx,
                        const char *variable_name,
                        const char *method_name,
                        const std::vector<DukFnArg> &args)
{
    const int top_on_entry = duk_get_top(ctx);

    duk_push_global_object(ctx);
    // [global]
    duk_get_prop_string(ctx, -1, variable_name);
    // [global] [variable]
    duk_push_string(ctx, method_name);
    // [global] [variable] [method_name]
    for (auto &a : args)
    {
        a.push(ctx);
    }
    // [global] [variable] [method_name] [args...]

    int ret = duk_pcall_prop(ctx, -((i32)args.size() + 2), (i32)args.size());
    // [global] [return_value]

    duk_swap(ctx, top_on_entry + 1, top_on_entry + 2);
    duk_set_top(ctx, top_on_entry + 1);
    // [return_value]

    if (pop_return_value)
    {
        duk_set_top(ctx, top_on_entry);
    }

    return ret == 0;
}

bool CallDukMethod::operator()(duk_context *ctx, int index_of_object, const char *method_name)
{
    const int top_on_entry = duk_get_top(ctx);

    if (index_of_object < 0)
    {
        index_of_object = top_on_entry + index_of_object;
    }

    duk_push_string(ctx, method_name);

    for (auto &a : args)
    {
        a.push(ctx);
    }

    int ret = duk_pcall_prop(ctx, index_of_object, (int)fo::size(args));
    // [object] ... [return_value]

    if (pop_return_value)
    {
        duk_set_top(ctx, top_on_entry);
    }

    return ret == 0;
}

bool get_prop_of_duk_variable(duk_context *ctx, const char *variable_name, const char *prop_name)
{
    const int top_on_entry = duk_get_top(ctx);

    int variable_exists = get_qualified_duk_variable(ctx, variable_name);
    if (!variable_exists)
    {
        LOG_F(ERROR, "Variable '%s' does not exist", variable_name);
        return false;
    }

    duk_bool_t has_prop = duk_get_prop_string(ctx, top_on_entry + 0, prop_name);

    if (!has_prop)
    {
        LOG_F(ERROR, "Variable '%s' does not have a property named '%s'", variable_name, prop_name);
        duk_set_top(ctx, top_on_entry);
        return false;
    }
    else
    {
        duk_copy(ctx, top_on_entry + 1, top_on_entry + 0);
        duk_set_top(ctx, top_on_entry + 1);
        return true;
    }
}

DukBufferInfo get_duk_buffer_info(duk_context *ctx, int index)
{
    const int top_on_entry = duk_get_top(ctx);

    LOG_F(INFO, "top_on_entry = %i", top_on_entry);

    if (index < 0)
    {
        index = top_on_entry + index;
    }

    LOG_F(INFO, "Index = %i", index);

    DukBufferInfo info;

    assert(duk_is_buffer_data(ctx, index));

    duk_get_prop_string(ctx, index, "length");

    info.num_elements = (u32)duk_to_number(ctx, -1);

    duk_get_prop_string(ctx, index, "BYTES_PER_ELEMENT");

    info.bytes_per_element = (u32)duk_to_number(ctx, -1);

    duk_size_t out_size;
    info.ptr = duk_get_buffer_data(ctx, index, &out_size);

    duk_set_top(ctx, top_on_entry);

    return info;
}

namespace custom_duk_heap
{

    void *my_alloc(void *ud, duk_size_t size)
    {
        // LOG_F(INFO, "Called my alloc with size %zu", (size_t)size);
        // DEFERSTAT(LOG_F(INFO, "Did allocate"));

        auto wc = reinterpret_cast<WrappedDukContext *>(ud);
        return wc->_allocator->allocate(size, fo::Allocator::DEFAULT_ALIGN);
    }

    void *my_realloc(void *ud, void *old, duk_size_t size)
    {
        auto wc = reinterpret_cast<WrappedDukContext *>(ud);
        return wc->_allocator->reallocate(old, size, fo::Allocator::DEFAULT_ALIGN);
    }

    void my_free(void *ud, void *ptr)
    {
        auto wc = reinterpret_cast<WrappedDukContext *>(ud);
        wc->_allocator->deallocate(ptr);
    }

    void my_fatal(void *ud, const char *ptr)
    {
        fo::TempAllocator2048 ta(fo::memory_globals::default_allocator());

        fo::string_stream::Buffer ss(ta);
        fo::reserve(ss, 1024);

        eng::print_callstack(ss);

        ABORT_F("Duktape fatal error, message -\n%s,\nC++ Callstack:\n",
            fo::string_stream::c_str(ss));
    }

} // namespace custom_duk_heap

void WrappedDukContext::init()
{
    CHECK_EQ_F(_context, nullptr);

    assert(_allocator != nullptr);

    _context = duk_create_heap(custom_duk_heap::my_alloc,
                   custom_duk_heap::my_realloc,
                   custom_duk_heap::my_free,
                   this,
                   custom_duk_heap::my_fatal);

    CHECK_NE_F(_context, nullptr, "Failed to create duktape context");

    if (_enable_console)
    {
        duk_console_init(_context, 0);
    }

    duk_module_duktape_init(_context);

    // Store a pointer to this `WrappedDukContext` object in the duktape context. This will allow
    // accessing the extra data in this object from C functions.
    duk_push_global_object(_context);              // [global]
    duk_push_pointer(_context, this);              // [global, &WrappedDukContext]
    duk_put_prop_literal(_context, -2, "wrapped_context_ptr"); // [global]
    duk_pop(_context);

    // Set the Duktape.modSearch function
    duk_get_global_string(_context, "Duktape");
    duk_push_c_function(_context, my_mod_search, 4 /*nargs*/);
    duk_put_prop_string(_context, -2, "modSearch");
    duk_pop(_context);

    LOG_F(INFO, "Initialized duktape context");

    add_module_path(LOGL_JS_MODULES_DIR);
}

void WrappedDukContext::push_source_file(const fs::path &path)
{
    fo::Array<char> text;
    read_file(path, text, true);
    duk_push_string(_context, fo::data(text)); // [source]
}

void WrappedDukContext::exec_file(const fs::path &path)
{
    self_.push_source_file(path);

    CHECK_EQ_F(duk_peval(_context), 0, "Duktape error - %s", duk_safe_to_string(_context, -1));

    // Don't care about the result.
    duk_pop(_context);
}

void WrappedDukContext::destroy()
{
    CHECK_NE_F(_context, nullptr);
    duk_destroy_heap(_context);
    _context = nullptr;
}

WrappedDukContext::~WrappedDukContext()
{
    if (_context)
    {
        if (_destroy_on_dtor)
        {
            destroy();
        }
        else
        {
            ABORT_F(
              "Dtor called on WrappedDukContext, but not destroying the duktape heap it owns");
        }
    }
}
