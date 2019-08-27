#include <learnogl/eng>

#include <duk_console.h>
#include <duktape.h>

#include <scaffold/timed_block.h>

static void push_file_as_string(duk_context *duk, const fs::path &file_path)
{
    fo::Array<char> text;
    read_file(file_path, text, true);
    duk_push_string(duk, fo::data(text));
}

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

static bool get_duk_qualified_variable(duk_context *duk, const std::vector<std::string> &qualified_name)
{
    duk_push_global_object(duk);

    const int n = (int)qualified_name.size();

    for (int i = 1; i <= n; ++i)
    {
        int exists = duk_get_prop_string(duk, -1, qualified_name[i - 1].c_str());
        if (!exists)
        {
            LOG_F(ERROR, "Qualified variable does not exist");
            duk_copy(duk, -1, -(i + 1));
            duk_set_top(duk, -(i + 1));
            return false;
        }
    }

    duk_copy(duk, -1, -(n + 1));
    duk_set_top(duk, -n);
    //
    return true;
}

// Constructs a new duktape object. If there is an error, returns false, otherwise returns true. On error, the
// stack is unmodified. On success, the newly constructed object resides on top.
static bool new_global_duk_object(duk_context *duk,
                  const std::vector<std::string> &qualified_ctor_name,
                  const std::vector<double> &arguments,
                  const char *global_variable_name)
{

    int exists = get_duk_qualified_variable(duk, qualified_ctor_name);

    if (!exists)
    {
        return false;
    }

    LOG_F(INFO,
          "Duk type, should be function - %s - %s",
          str_duk_type(duk_get_type(duk, -1)),
          duk_is_function(duk, -1) ? "yes" : "no");

    // Push each argument
    for (double a : arguments)
    {
        duk_push_number(duk, a);
    }

    int error = duk_pnew(duk, arguments.size());
    CHECK_EQ_F(error, 0);

    // [new_object]
    duk_push_string(duk, global_variable_name);
    duk_push_global_object(duk);
    // [new_object] [variable_name] [global]

    duk_swap(duk, -1, -3);
    // [global] [variable_name] [new_object]

    duk_put_prop(duk, -3);

    duk_pop_n(duk, 1);
    // empty

    return true;
}

// Calls the duktape method on an object that should be on top of stack.
static bool call_duk_method(duk_context *duk,
                const char *variable_name,
                const char *method_name,
                const std::vector<double> &args)
{

    duk_push_global_object(duk);
    duk_get_prop_string(duk, -1, variable_name);
    duk_push_string(duk, method_name);

    LOG_F(INFO,
          "Duk type, should be string - %s - %s",
          str_duk_type(duk_get_type(duk, -1)),
          duk_is_string(duk, -1) ? "yes" : "no");

    for (auto &a : args)
    {
        duk_push_number(duk, a);
    }

    int ret = duk_pcall_prop(duk, -((i32)args.size() + 2), (i32)args.size());
    if (ret != 0)
    {
        LOG_F(ERROR, "Duktape error - %s", duk_safe_to_string(duk, -1));
        duk_pop_n(duk, 1);
    }
    else
    {
        CHECK_EQ_F(duk_get_type(duk, -1), DUK_TYPE_NUMBER);
        double result = duk_get_number(duk, -1);
        LOG_F(INFO, "Result = %f", result);
    }

    return ret == 0;
}

int main()
{
    eng::init_memory();
    DEFERSTAT(eng::shutdown_memory());

    duk_context *duk = duk_create_heap_default();
    DEFERSTAT(duk_destroy_heap(duk));

    duk_console_init(duk, 0);

    push_file_as_string(duk, make_path(SOURCE_DIR, "pascals_triangle.js"));

    {
        TIMED_BLOCK;
        CHECK_EQ_F(duk_peval(duk), 0, "Duktape error - %s", duk_safe_to_string(duk, -1));
    }

    LOG_F(INFO, "File eval result type = %s", str_duk_type(duk_get_type(duk, -1)));
    duk_pop_n(duk, 1);

    // Construct a PascalsTriangle object
    new_global_duk_object(duk, { "PascalsTriangle" }, { 9 }, "g_pt");

    call_duk_method(duk, "g_pt", "get", { 9, 4 });

    timedblock::print_record_table(stdout);
}
