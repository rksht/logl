#include <learnogl/console.h>
#include <learnogl/stopwatch.h>
#include <learnogl/gl_misc.h>

using namespace fo;
using namespace math;

#define DEF_ALLOCATOR memory_globals::default_allocator()

#define LUA_TEST 0
#define TEXTFILE_TEST 1

#if LUA_TEST
#include <lua.hpp>
#include <signal.h> // for lua repl
#endif

#if LUA_TEST

struct LuaReplState {
    lua_State *L;

    std::string _cur_command;

    LuaReplState() {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    ~LuaReplState() {
        lua_close(L);
        L = nullptr;
    }
};

struct TestAppState {
    LuaReplState &repl;
    console::Console &console;
};

// Lua stack dump result
std::vector<std::string> stackdump(LuaReplState &repl) {
    auto L = repl.L;

    int n = lua_gettop(L);

    std::vector<std::string> results;

    if (n == 0) {
        return results;
    }

    for (int i = 1; i <= n; ++i) {
        int t = lua_type(L, i);

        switch (t) {
        case LUA_TSTRING: {
            results.push_back(std::string(lua_tostring(L, i)));
        } break;

        case LUA_TBOOLEAN: {
            results.push_back(lua_toboolean(L, i) ? "true" : "false");
        } break;

        case LUA_TNUMBER: {
            results.push_back(std::to_string(lua_tonumber(L, i)));
        } break;

        default: { results.push_back(std::string(lua_typename(L, t))); } break;
        }
    }
    return results;
}

static lua_State *globalL = NULL;

static const char *progname = "lua";
/* mark in error messages for incomplete statements */
#define EOFMARK "<eof>"
#define marklen (sizeof(EOFMARK) / sizeof(char) - 1)

/*
** Hook set by signal function to stop the interpreter.
*/
static void lstop(lua_State *L, lua_Debug *ar) {
    (void)ar;                   /* unused arg. */
    lua_sethook(L, NULL, 0, 0); /* reset hook */
    luaL_error(L, "interrupted!");
}

/*
** Function to be called at a C signal. Because a C signal cannot
** just change a Lua state (as there is no proper synchronization),
** this function only sets a hook that, when called, will stop the
** interpreter.
*/
static void laction(int i) {
    signal(i, SIG_DFL); /* if another SIGINT happens, terminate process */
    lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}

/*
** Check whether 'status' signals a syntax error and the error
** message at the top of the stack ends with the above mark for
** incomplete statements.
*/
static int incomplete(LuaReplState &repl, int status) {
    auto L = repl.L;
    if (status == LUA_ERRSYNTAX) {
        size_t lmsg;
        const char *msg = lua_tolstring(L, -1, &lmsg);
        if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0) {
            lua_pop(L, 1);
            return 1;
        }
    }
    return 0; /* else... */
}

/*
** Prompt the user, read a line, and push it into the Lua stack.
*/
static int pushline(LuaReplState &repl, int firstline) {
    auto L = repl.L;
    lua_pushlstring(L, repl._cur_command.c_str(), repl._cur_command.size());
    return 1;
}

/*
** Try to compile line on the stack as 'return <line>;'; on return, stack
** has either compiled chunk or original line (if compilation failed).
*/
static int addreturn(LuaReplState &repl) {
    auto L = repl.L;

    const char *line = lua_tostring(L, -1); /* original line */
    const char *retline = lua_pushfstring(L, "return %s;", line);
    int status = luaL_loadbuffer(L, retline, strlen(retline), "=stdin");
    if (status == LUA_OK) {
        lua_remove(L, -2); /* remove modified line */
#if 0
        if (line[0] != '\0')       /* non empty? */
            lua_saveline(L, line); /* keep pager */
#endif
    } else
        lua_pop(L, 2); /* pop result from 'luaL_loadbuffer' and modified line */
    return status;
}

/*
** Read multiple lines until a complete Lua statement
*/
static int multiline(LuaReplState &repl) {
    auto L = repl.L;

    for (;;) { /* repeat until gets a complete statement */
        size_t len;
        const char *line = lua_tolstring(L, 1, &len);         /* get what it has */
        int status = luaL_loadbuffer(L, line, len, "=stdin"); /* try it */
        if (!incomplete(repl, status) || !pushline(repl, 0)) {
#if 0
            lua_saveline(L, line); /* keep pager */
#endif
            return status; /* cannot or should not try to add continuation line */
        }
        lua_pushliteral(L, "\n"); /* add newline... */
        lua_insert(L, -2);        /* ...between the two lines */
        lua_concat(L, 3);         /* join them */
    }
}

static int loadline(LuaReplState &repl) {
    auto L = repl.L;

    int status;
    lua_settop(L, 0);
    if (!pushline(repl, 1))
        return -1;                            /* no input */
    if ((status = addreturn(repl)) != LUA_OK) /* 'return ...' did not work? */
        status = multiline(repl);             /* try as command, maybe with continuation lines */
    lua_remove(L, 1);                         /* remove line from the stack */
    lua_assert(lua_gettop(L) == 1);
    return status;
}

/*
** Message handler used to run all chunks
*/
static int msghandler(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) {                           /* is error object not a string? */
        if (luaL_callmeta(L, 1, "__tostring") && /* does it have a metamethod */
            lua_type(L, -1) == LUA_TSTRING)      /* that produces a string? */
            return 1;                            /* that is the message */
        else
            msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1); /* append a standard traceback */
    return 1;                     /* return the traceback */
}

static void l_message(const char *pname, const char *msg) {
    if (pname)
        lua_writestringerror("%s: ", pname);
    lua_writestringerror("%s\n", msg);
}

std::string report(LuaReplState &repl, int status) {
    std::string s;

    if (status != LUA_OK) {
        const char *msg = lua_tostring(repl.L, -1);
        s += msg;
        // l_message(progname, msg);
        lua_pop(repl.L, 1); /* remove message */
    }
    // return status;
    return s;
}

/*
** Interface to 'lua_pcall', which sets appropriate message function
** and C-signal handler. Used to run all chunks.
*/
static int docall(LuaReplState &repl, int narg, int nres) {
    auto L = repl.L;
    int status;
    int base = lua_gettop(L) - narg;  /* function index */
    lua_pushcfunction(L, msghandler); /* push message handler */
    lua_insert(L, base);              /* put it under function and args */
    globalL = L;                      /* to be available to 'laction' */
    signal(SIGINT, laction);          /* set C-signal handler */
    status = lua_pcall(L, narg, nres, base);
    signal(SIGINT, SIG_DFL); /* reset C-signal handler */
    lua_remove(L, base);     /* remove message handler from the stack */
    return status;
}

void do_command(LuaReplState &repl, const std::string &lua_command, console::Console &console) {
    LOG_F(INFO, "Doing command: %s", lua_command.c_str());

    auto L = repl.L;

    repl._cur_command = lua_command;

    std::vector<std::string> results;

    int status;
    const char *oldprogname = progname;
    progname = NULL; /* no 'progname' on errors in interactive mode */
    if ((status = loadline(repl)) != -1) {
        if (status == LUA_OK)
            status = docall(repl, 0, LUA_MULTRET);
        if (status == LUA_OK) {
            // l_print(L);
            results = stackdump(repl);
        } else {
            results.push_back(report(repl, status));
        }
    }
    lua_settop(L, 0); /* clear stack */
    lua_writeline();
    progname = oldprogname;

    Array<font::AlignedQuad> aligned_quads{DEF_ALLOCATOR};

    for (auto result : results) {
        clear(aligned_quads);

        // For each line, make line quads and add it to pager
        add_string_to_pager(console, result.c_str(), result.size());
    }
}

void on_key_press(GLFWwindow *window, int key, int scancode, int action, int mods) {
    TestAppState &app = *reinterpret_cast<TestAppState *>(glfwGetWindowUserPointer(window));
    auto &console = app.console;
    LuaReplState &repl = app.repl;

    switch (key) {
    case GLFW_KEY_BACKSPACE: {
        switch (action) {
        case GLFW_PRESS:
            console_backspace(console);
            break;
        default:
            break;
        }
    } break;

    case GLFW_KEY_ENTER: {
        switch (action) {
        case GLFW_PRESS: {
            auto prompt_string = console_input(console, '\n');
            if (prompt_string) {
                auto &command = prompt_string.value();
                do_command(repl, command, console);
            }
        } break;

        default:
            break;
        }
        break;

    case GLFW_KEY_UP: {
        if (GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
            console::scroll_up_pager(console);
        }
    } break;

    case GLFW_KEY_DOWN: {
        if (GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
            console::scroll_down_pager(console);
        }
    } break;

    default:
        break;
    }
    }
}

void on_text_input(GLFWwindow *window, unsigned int codepoint, int mods) {
    TestAppState &app = *reinterpret_cast<TestAppState *>(glfwGetWindowUserPointer(window));
    auto &console = app.console;
    LuaReplState &repl = app.repl;

    auto prompt_string = console_input(console, codepoint);
}

void okgo() {
    GLFWwindow *window;

    StartGLParams glparams;
    eng::GLApp *gl;

    glparams.window_width = 1366;
    glparams.window_height = 768;
    glparams.window_title  = "console test";
    glparams.enable_debug_output = true;

    auto gl = eng::start_gl(glparams, eng::gl());

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);

    // Create a console
    console::Console console{};
    init(console, window_width, window_height, 0.6f, 20, 0, 0, 1000, 1,
         LOGL_DATA_DIR "/iosevka-term-heavy.ttf");

    LuaReplState repl;

    TestAppState app{repl, console};

    glfwSetWindowUserPointer(window, &app);

    glfwSetKeyCallback(window, on_key_press);
    glfwSetCharModsCallback(window, on_text_input);

    do_command(repl, "f = io.open('Makefile', 'r')", console);
    do_command(repl, "s = f:read('*all')", console);

    add_string_to_pager(console, "Hello world");
    add_fmt_string_to_pager(console, "Hello world %i again ", 1000);

    const auto vs = R"(
    #version 430 core

    layout(location = 0) in vec2 pos;

    void main() {
        gl_Position = vec4(pos.xy, -1.0, 1.0);
    }
    )";

    const auto fs = R"(
    #version 430 core

    uniform float time;
    const float period = 4.0f;
    const float pi = 3.142;

    out vec4 fc;

    void main() {
        vec2 xy = gl_FragCoord.xy;
        xy /= 1024.0;

        float t = sin(2 * pi * time / period);
        t = (t + 1.0) / 2;
        fc = vec4(xy.x * t, 0.0, xy.y, 1.0);
    }

    )";

    Vector2 square[] = {{-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}};

    GLuint vbo, vao;
    glGenBuffers(1, &vbo);
    glGenVertexArrays(1, &vao);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(square), square, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vector2), 0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    auto program = eng::create_program(vs, fs);

    auto time_loc = glGetUniformLocation(program, "time");

    float total_time = 0.0f;

    stop_watch::State<std::chrono::high_resolution_clock> sw{};
    stop_watch::start(sw);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float t = std::chrono::duration<float, std::ratio<1, 1>>(stop_watch::restart(sw)).count();
        total_time += t;

        glViewport(0, 0, console._screen_width, console._screen_height);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glBindVertexArray(vao);
        glUseProgram(program);

        glUniform1f(time_loc, total_time);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        draw_prompt(console);
        draw_pager_updated(console);
        blit_pager(console);

        glfwSwapBuffers(window);
    }
}

#elif TEXTFILE_TEST

constexpr auto text_file_path = SOURCE_DIR "/console_test.cpp";

void on_key_press(GLFWwindow *window, int key, int scancode, int action, int mods) {
    auto &console = *reinterpret_cast<console::Console *>(glfwGetWindowUserPointer(window));

    switch (key) {
    case GLFW_KEY_BACKSPACE: {
        switch (action) {
        case GLFW_PRESS:
            console_backspace(console);
            break;
        default:
            break;
        }
    } break;

    case GLFW_KEY_ENTER: {
        switch (action) {
        case GLFW_PRESS: {
            auto prompt_string = console_input(console, '\n');
            if (prompt_string) {
                LOG_F(INFO, "Prompt string = %s", prompt_string.value().c_str());
            }
        } break;
        default:
            break;
        }
        break;

    case GLFW_KEY_UP: {
        if (GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
            // LOG_F(INFO, "Scrolling pager up");
            console::scroll_up_pager(console);
        }
    } break;

    case GLFW_KEY_DOWN: {
        if (GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
            // LOG_F(INFO, "Scrolling pager down");
            console::scroll_down_pager(console);
        }
    } break;

    default:
        break;
    }
    }
}

void on_text_input(GLFWwindow *window, unsigned int codepoint, int mods) {
    auto &console = *reinterpret_cast<console::Console *>(glfwGetWindowUserPointer(window));
    auto prompt_string = console_input(console, codepoint);
}

static eng::StartGLParams start_gl_params;

void okgo() {
    const i32 window_width = 1366;
    const i32 window_height = 768;

    start_gl_params.window_width = 1366;
    start_gl_params.window_height = 768;

    eng::start_gl(start_gl_params, eng::gl());
    DEFERSTAT(eng::close_gl(start_gl_params, eng::gl()));
    
    eng::enable_debug_output(nullptr, nullptr, true);

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);

    // Create a console
    console::Console console;
    // init(console, window_width, window_height, 0.7f, 20, 20);
    console::init(console, eng::gl().bs, window_width, window_height, 0.7f, 15, 1000);

    glfwSetWindowUserPointer(eng::gl().window, &console);

    glfwSetKeyCallback(eng::gl().window, &on_key_press);
    glfwSetCharModsCallback(eng::gl().window, on_text_input);

    {
        glfwSwapInterval(0);

        Array<u8> text_file{DEF_ALLOCATOR};

        read_file(text_file_path, text_file, true);

        // Push in the lines

        std::string line;

        Array<font::AlignedQuad> line_quads{DEF_ALLOCATOR};
        reserve(line_quads, 90);

        for (u32 i = 0; i < size(text_file); ++i) {
            auto ch = text_file[i];
            if (ch != '\n') {
                assert(0 <= ch < 128);
                line += (i32)ch;
                continue;
            }

            if (line.size() != 0) {
                make_line_quads(console, line.c_str(), line.size(), line_quads);
                // Check ascii
                CHECK_F(line.size() == size(line_quads),
                        "len(line) and len(quads) mismatch for line: %s", line.c_str());
                add_line_to_pager(console, data(line_quads), size(line_quads));

                glViewport(0, 0, console._screen_width, console._screen_height);

                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                draw_prompt(console);
                draw_pager_updated(console);
                blit_pager(console);

                glfwSwapBuffers(eng::gl().window);

                // getchar();
            }

            line.clear();
            clear(line_quads);
        }
    }

    glfwSwapInterval(1);

    default_binding_state().seal();
    while (!glfwWindowShouldClose(eng::gl().window)) {
        glfwWaitEvents();

        glViewport(0, 0, console._screen_width, console._screen_height);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        draw_prompt(console);
        draw_pager_updated(console);
        blit_pager(console);

        glfwSwapBuffers(eng::gl().window);
    }
}

#endif

int main() {
    memory_globals::init();
    DEFER( []() { memory_globals::shutdown(); });

    okgo();
}
