// A console for logging and taking simple commands as input in-game.

#pragma once

// clang-format off
#include <learnogl/stb_rect_pack.h>
#include <learnogl/stb_truetype.h>
// clang-format on
#include <scaffold/types.h>
#include <scaffold/collection_types.h>
#include <scaffold/queue.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/gl_misc.h>
#include <learnogl/font.h>
#include <learnogl/input_handler.h>
#include <learnogl/gl_binding_state.h>

#ifndef LOGL_UI_FONT
#    error "Need path to default console font"
#endif

namespace console {

struct Prompt {
    PaddedRect _box;
    i32 _max_chars;

    // The quads for each character currently in prompt. Stored so that we can directly copy to the vbuffer
    // and glDrawArrays can be used to draw.
    fo::Array<eng::font::AlignedQuad> _aligned_quads{fo::memory_globals::default_allocator()};

    // Each of the characters currently in the prompt, including the prompt string.
    fo::Array<i32> _chars{fo::memory_globals::default_allocator()};

    fo::Vector2 _next_glyph_topleft;

    GLuint _prompt_verts_vbo = 0;
    GLuint _prompt_program_handle = 0;

    GLuint _glyph_vertex_vao = 0;

    const char *_prompt_string = "> ";

    GLuint _cursor_program_handle = 0;

    std::vector<std::string> _previous_inputs;
};

struct LineRange {
    u32 chars_before_line;
    u32 chars_in_line;
};

struct Console {
    Console() = default;
    ~Console();
    Console(const Console &) = delete;
    Console(Console &&) = delete;
    Console &operator=(Console &&) = delete;

    i32 _screen_width;
    i32 _screen_height;

    eng::font::FontData _font;

    GLuint _line_constants_ubo_binding;
    GLuint _pager_texture_unit;

    i32 _max_lines_in_pager_queue; // If the line queue exceeds this size, we try to remove old lines.
    i32 _max_lines_showable;
    i32 _max_scrollback_lines;
    i32 _pager_width;
    i32 _pager_height;
    eng::font::AlignedQuad _pager_screen_quad;

    Prompt _prompt;

    // Console to screen ratio for height
    f32 _height_ratio;

    // Line ranges in the pager are kept in a queue.
    fo::Queue<LineRange> _lines_queue{fo::memory_globals::default_allocator()};

    // The pager gets rendered into a fbuffer first, and then blitted onto the on-screen fbuffer.
    eng::FBO _pager_fbo;
    GLuint _pager_tex;
    GLuint _pager_depth_rbo;

    GLuint _pager_program_handle = 0;
    GLuint _blit_program_handle = 0;

    GLuint _pager_verts_vbo = 0;
    GLuint _pager_line_verts_ebo = 0;

    u32 _pager_verts_vbo_size; // Number of AlignedQuads the pager vbo is capable of holding
    u32 _pager_line_ebo_size;  // Number of AlignedQuads we can draw using the current ubo

    i32 _scroll_offset_lines; // Scroll offset in units of line advance

    // We store the quad vertices in a queue for easier popping.
    fo::Queue<eng::font::AlignedQuad> _pager_quads_queue{fo::memory_globals::default_allocator()};

    // VAO representing the format of glyph quad vertices.
    GLuint _glyph_vertex_vao;

    GLuint _line_constants_ubo;

    bool _pager_state_undrawn = false;

    // fo::Array<u8> _debug_image{fo::memory_globals::default_allocator()};
};

void init(Console &c,
          eng::BindingState &bs,
          i32 screen_width,
          i32 screen_height,
          f32 height_ratio,
          f32 pixels_per_max_height,
          i32 max_scrollback_lines = 1000,
          i32 oversamples = 1,
          const fs::path &ttf_file = LOGL_UI_FONT);

// Takes a character as input and displays on the prompt. Returns the full prompt string if newline is given
// as the input character.
optional<std::string> console_input(Console &c, i32 codepoint);

// Enters the full string into the prompt.
void console_input(Console &c, const char *line, u32 length);

// Enters the formatted string into the prompt.
void console_fmt_input(Console &c, const char *fmt, ...);

// Resets the prompt
void reset_console_prompt(Console &c);

// Enters a backspace into the prompt (deletes previous character)
void console_backspace(Console &c);

// Pushes character quads for the line given into the array. Then you send this array to `add_line_to_pager`.
eng::font::PushQuadReturn
make_line_quads(Console &c, const char *line, u32 num_chars, fo::Array<eng::font::AlignedQuad> &quads_out);

// Adds given line quads to the pager.
void add_line_to_pager(Console &c, const eng::font::AlignedQuad *char_quads_in_line, u32 num_chars);

// Pushes given string into the pager. Default length is -1 that denotes length is `strlen(string)`.
void add_string_to_pager(Console &c, const char *string, i32 length = -1);

// Pushes the given formatted string into the pager
void add_fmt_string_to_pager(Console &c, const char *fmt, ...);

// Scroll the pager
void scroll_up_pager(Console &c);
void scroll_down_pager(Console &c);

// Draw the prompt.
void draw_prompt(Console &c);

// Draw the prompt. Since we draw to a separate framebuffer, we will change gl state. Usually you will call
// this with GL_FRAMEBUFFER set to the default framebuffer, so they are the default values of arguments.
void draw_pager_updated(Console &c, GLuint current_draw_framebuffer = 0);

// Blit the pager quad onto the screen
void blit_pager(Console &c);

} // namespace console

// -- An input handler for parsing prompt input line by line.

// Console command handler template. Can be reused by any app. The CallbackFn will be called on a command that
// has been input. Here's how the call looks like - `cb(console, std string)`. The MakeNextHandlerFn is
// function that will be called to create the next handler after the console exits when you press ESC.
template <typename AppType, typename CallbackFn, typename MakeNextHandlerFn>
class ConsoleCommandHandler : public eng::input::InputHandlerBase<AppType> {
  public:
    console::Console *_console;
    CallbackFn _cb;
    MakeNextHandlerFn _make_next_fn;

    // GLFW calls the character input callback later, so we don't take the character as input at first.
    bool _is_first_char = true;

    ConsoleCommandHandler(console::Console &console, CallbackFn cb, MakeNextHandlerFn make_next_fn)
        : _console(&console)
        , _cb(std::move(cb))
        , _make_next_fn(std::move(make_next_fn)) {}

    void handle_on_key(AppType &app, eng::input::OnKeyArgs args) override {
        if (args.key == GLFW_KEY_ESCAPE && args.action == GLFW_PRESS) {
            app.set_input_handler(_make_next_fn());
            return;
        }

        if (args.key == GLFW_KEY_BACKSPACE && args.action == GLFW_PRESS) {
            console::console_backspace(*_console);
            return;
        }

        if (args.key == GLFW_KEY_ENTER && args.action == GLFW_PRESS) {
            auto console_ret = console::console_input(*_console, '\n');
            if ((bool)console_ret) {
                _cb(*_console, console_ret.value());
            }
        }
    }

    void handle_on_char_input(AppType &app, eng::input::OnCharInputArgs args) override {
        if (_is_first_char) {
            _is_first_char = false;
            return;
        }

        auto console_ret = console::console_input(*_console, args.codepoint);

        if ((bool)console_ret) {
            _cb(*_console, console_ret.value());
        }
    }

    ~ConsoleCommandHandler() { _console = nullptr; }
};
