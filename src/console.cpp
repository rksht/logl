#include <learnogl/console.h>
#include <learnogl/gl_binding_state.h>
#include <learnogl/shader.h>
#include <scaffold/queue.h>

#include <stdarg.h>

using namespace fo;
using namespace eng::math;
using namespace console;

#define DEFAULT_ALLOCATOR memory_globals::default_allocator()

// Some options that will affect all `Console` objects.

static constexpr bool USE_PRIMITIVE_RESTART = true;

// The pager will be transparent
static constexpr bool USE_TRANSPARENT_PAGER = true;

static void init_prompt(
    Prompt &p, f32 prompt_top_left_y, i32 console_width, i32 character_height, f32 advance_width_in_pixels);
static void reset_prompt(Prompt &p);
static void upload_prompt_vbo(Console &c);

static void init_shader_program(Console &c);
static void init_shader_buffers(Console &c, eng::BindingState &bs);
static void reallocate_pager_vbo(Console &c);
static void set_prompt_string(Console &c);

void init_prompt(
    Prompt &p, f32 prompt_top_left_y, i32 console_width, i32 character_height, f32 advance_width_in_pixels) {
    // Initialize the vao for the glyph quad vertices
    glGenVertexArrays(1, &p._glyph_vertex_vao);
    glBindVertexArray(p._glyph_vertex_vao);
    glVertexAttribFormat(
        0, 2, GL_FLOAT, GL_FALSE, offsetof(eng::font::GlyphQuadVertexData, position_screen_space));
    glVertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, offsetof(eng::font::GlyphQuadVertexData, uv));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribBinding(0, 0);
    glVertexAttribBinding(1, 0);

    p._max_chars = i32(console_width / advance_width_in_pixels);

    constexpr f32 padding = 4.f;

    p._box.set_topleft_including_padding(
        Vector2{0.f, prompt_top_left_y}, Vector2{(f32)console_width, character_height + padding}, padding);

    reset_prompt(p);

    LOG_F(INFO, "Max chars in prompt = %i", p._max_chars);
}

void reset_prompt(Prompt &p) {
    p._next_glyph_topleft = {0.0f, 0.0f}; // We add the padding and height amount via the shader
    clear(p._aligned_quads);
    clear(p._chars);

    if (p._prompt_verts_vbo == 0) {
        glGenBuffers(1, &p._prompt_verts_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, p._prompt_verts_vbo);
        glBufferData(
            GL_ARRAY_BUFFER, (p._max_chars + 1) * sizeof(eng::font::AlignedQuad), nullptr, GL_DYNAMIC_DRAW);
        // ^^ The + 1 is for cursor.
    }

    // xyspoon: A state that will be maintained always is that the size(p._aligned_quads) == size(p._chars).
}

void set_up_pager(Console &c, eng::BindingState &bs) {
    c._max_lines_in_pager_queue =
        std::max(10, c._screen_height / c._font.bbox_line_advance_pixels + c._max_scrollback_lines);

    const u32 average_chars_per_line = 100; // Arbitrary
    c._pager_verts_vbo_size = c._max_lines_in_pager_queue * average_chars_per_line;

    c._pager_width = c._screen_width;
    c._pager_height = c._screen_height * c._height_ratio;
    c._max_lines_showable = c._pager_height / c._font.pixels_per_max_height;

    LOG_SCOPE_F(INFO, "Setting up console and pager");

    LOG_F(INFO,
          "max_lines_in_pager = %i, max_lines_showable = %i",
          c._max_lines_in_pager_queue,
          c._max_lines_showable);

    auto pager_tex_format = GL_R8;
    if (USE_TRANSPARENT_PAGER) {
        pager_tex_format = GL_RGBA8;
    }

    glGenTextures(1, &c._pager_tex);
    c._pager_texture_unit = bs.bind_unique(eng::gl_desc::SampledTexture(c._pager_tex));
    LOG_F(INFO, "Pager texture unit = %u", c._pager_texture_unit);

    glBindTexture(GL_TEXTURE_2D, c._pager_tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, pager_tex_format, c._pager_width, c._pager_height);

    c._pager_fbo.gen().bind().add_attachment(0, c._pager_tex).set_done_creating();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Reserve some space for the pager vertices using a heuristic. I'm guessing most lines are less than
    // 2/3 the max line length, so this should be more than enough.
    reserve(c._pager_quads_queue,
            (u32)std::ceil(c._max_lines_in_pager_queue * c._prompt._max_chars * 2.0 / 3));
    reallocate_pager_vbo(c);

    // This is an index buffer we use for drawing each line using primitive restart. The longer the maximum
    // length line is, the longer the ebo grows.
    glGenBuffers(1, &c._pager_line_verts_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c._pager_line_verts_ebo);
    c._pager_line_ebo_size = 0;

    c._scroll_offset_lines = 0;

    PaddedRect pager_rect;
    pager_rect.set_topleft_including_padding(
        Vector2{0.f, 0.f}, Vector2{(f32)c._screen_width, (f32)c._pager_height}, 0.0f);
}

void init_shader_program(Console &c) {
    eng::ShaderDefines defs;

    if (USE_TRANSPARENT_PAGER) {
        defs.add("USE_TRANSPARENT_PAGER", 1);
    }

    const auto vs_path = fs::path(LOGL_SHADERS_DIR) / "console_vs.vert";
    const auto fs_path = fs::path(LOGL_SHADERS_DIR) / "console_fs.frag";

    GLuint vs, fs;

    const auto create_vs_fs = [&]() {
        vs = create_shader_object(
            eng::CreateShaderOptionBits::k_prepend_version,
            {eng::ShaderSourceType(defs.get_string().c_str()), eng::ShaderSourceType(vs_path)},
            GL_VERTEX_SHADER);

        fs = create_shader_object(
            eng::CreateShaderOptionBits::k_prepend_version,
            {eng::ShaderSourceType(defs.get_string().c_str()), eng::ShaderSourceType(vs_path)},
            GL_FRAGMENT_SHADER);
    };

    defs.add("VIEWPORT_WIDTH", c._screen_width)
        .add("VIEWPORT_HEIGHT", c._screen_height)
        .add("PROMPT_FS", 1)
        .add("FONT_ATLAS_TEXUNIT", (int)c._font.texture_unit)
        .add("LINE_CONSTANTS_UBO_BINDING", (int)c._line_constants_ubo_binding)
        .add("PAGER_TEXTURE_UNIT", (int)c._pager_texture_unit);

    DLOG_F(INFO, "\n%s", defs.get_string().c_str());

    create_vs_fs();
    c._prompt._prompt_program_handle = eng::create_program(vs, fs);

    defs.remove("PROMPT_FS").add("CURSOR_FS", 1);
    create_vs_fs();
    c._prompt._cursor_program_handle = eng::create_program(vs, fs);

    defs.remove("CURSOR_FS").add("PAGER_QUAD_FS", 1);
    create_vs_fs();
    c._blit_program_handle = eng::create_program(vs, fs);

    defs.add("VIEWPORT_WIDTH", c._pager_width)
        .add("VIEWPORT_HEIGHT", c._pager_height)
        .remove("PAGER_QUAD_FS")
        .add("PAGER_FBO_FS", 1);

    create_vs_fs();
    c._pager_program_handle = eng::create_program(vs, fs);
}

void init_shader_buffers(Console &c, eng::BindingState &bs) {
    glGenBuffers(1, &c._line_constants_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, c._line_constants_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Vector2), nullptr, GL_DYNAMIC_DRAW);
    c._line_constants_ubo_binding =
        bs.bind_unique(eng::gl_desc::UniformBuffer(c._line_constants_ubo, 0, sizeof(Vector2)));

    LOG_F(INFO, "_line_constants_ubo_binding = %u", c._line_constants_ubo_binding);
}

void reallocate_pager_vbo(Console &c) {
    static u32 times_vbo_reallocated = 0;
    ++times_vbo_reallocated;
    if (c._pager_verts_vbo == 0) {
        glGenBuffers(1, &c._pager_verts_vbo);
    }

    glBindBuffer(GL_ARRAY_BUFFER, c._pager_verts_vbo);
    glBufferData(
        GL_ARRAY_BUFFER, c._pager_verts_vbo_size * sizeof(eng::font::AlignedQuad), nullptr, GL_DYNAMIC_DRAW);

    LOG_F(INFO,
          "Times pager vbo reallocated = %u, New size = %.0f KB, (%u chars)",
          times_vbo_reallocated,
          c._pager_verts_vbo_size * sizeof(eng::font::AlignedQuad) / float(1024),
          c._pager_verts_vbo_size);
}

void set_prompt_string(Console &c) {
    u32 prompt_string_length = strlen(c._prompt._prompt_string);

    for (u32 i = 0; i < prompt_string_length; ++i) {
        console_input(c, c._prompt._prompt_string[i]);
    }
}

void upload_prompt_vbo(Console &c) {
    auto &p = c._prompt;

    // Create the new cursor quad and append it to the vbo. Yea, we store it in the same vbo.
    PaddedRect cursor_box;
    cursor_box.set_topleft_including_padding(
        p._next_glyph_topleft, Vector2{c._font.advance_width, c._font.pixels_per_max_height}, 0.0f);

    eng::font::AlignedQuad cursor_quad;
    cursor_quad.set_from_padded_rect(cursor_box);
    push_back(p._aligned_quads, cursor_quad);

    // Upload the character quads and the cursor quad to the prompt vbo
    glBindBuffer(GL_ARRAY_BUFFER, p._prompt_verts_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vec_bytes(p._aligned_quads), data(p._aligned_quads));

    // Pop the cursor quad.
    pop_back(p._aligned_quads);
}

namespace console {

Console::~Console() { _max_lines_in_pager_queue = 0; }

void init(Console &c,
          eng::BindingState &bs,
          i32 screen_width,
          i32 screen_height,
          f32 height_ratio,
          f32 pixels_per_max_height,
          i32 max_scrollback_lines,
          i32 oversamples,
          const fs::path &ttf_file) {
    c._screen_width = screen_width;
    c._screen_height = screen_height;
    c._height_ratio = height_ratio;

    std::vector<eng::font::CodepointRange> range{{0, 256}};

    c._max_scrollback_lines = max_scrollback_lines;
    eng::font::init(c._font, bs, ttf_file, pixels_per_max_height, range.data(), range.size(), oversamples);

    init_prompt(c._prompt,
                c._screen_height * c._height_ratio + c._font.bbox_line_advance_pixels + 2.0f,
                c._screen_width,
                c._font.pixels_per_max_height,
                c._font.advance_width);
    c._glyph_vertex_vao = c._prompt._glyph_vertex_vao;

    init_shader_buffers(c, bs);
    set_up_pager(c, bs);
    init_shader_program(c);
    set_prompt_string(c);
}

void reset_console_prompt(Console &c) { reset_prompt(c._prompt); }

eng::font::PushQuadReturn
make_line_quads(Console &c, const char *line, u32 num_chars, Array<eng::font::AlignedQuad> &quads_out) {
    const u32 initial_array_size = size(quads_out);
    resize(quads_out, initial_array_size + num_chars);

    TempAllocator512 ta(DEFAULT_ALLOCATOR);
    Array<i32> codepoints(ta, num_chars);
    std::copy(line, line + num_chars, begin(codepoints));

    eng::font::PushQuadReturn ret = eng::font::push_line_quads(c._font,
                                                               data(codepoints),
                                                               size(codepoints),
                                                               0.0f,
                                                               (f32)c._screen_width,
                                                               0.0f,
                                                               data(quads_out) + initial_array_size);

    if (ret.num_chars_pushed != num_chars) {
        LOG_F(WARNING, "Could not push all characters in line: '%.*s'", num_chars, line);
    }

    return ret;
}

// Number of lines that we can remove from the console queue, taking into account the scrollback amount.
static i32 num_lines_removable(const Console &c) {
    return std::max(0, i32(size(c._lines_queue) - (c._max_scrollback_lines + c._max_lines_showable)));
}

// Adds a new line to pager and slides the "window" so that the last console size
void add_line_to_pager(Console &c, const eng::font::AlignedQuad *char_quads_in_line, u32 num_chars) {
    c._pager_state_undrawn = true;

    bool can_append_to_vbo = true;

    const u32 quad_count_before_push = size(c._pager_quads_queue);

    // An attempt at removing old quads. See if number of lines in queue is more than
    // _max_lines_in_pager_queue, and if so, remove out of scrollback lines if there are any.
    if ((i32)size(c._lines_queue) >= c._max_lines_in_pager_queue) {
        i32 lines_to_remove = num_lines_removable(c);

        if (lines_to_remove == 0) {
            LOG_F(INFO, "Cannot remove lines out of scrollback");
        }

        // Pop the removable line ranges and quads
        const auto last = c._lines_queue[lines_to_remove - 1];
        const u32 chars_outside_pager = last.chars_before_line + last.chars_in_line;
        consume(c._pager_quads_queue, chars_outside_pager);
        consume(c._lines_queue, lines_to_remove);

        // Set `chars_before_line` for each line appropriately
        u32 chars_before_line = 0;

        for (u32 i = 0; i < size(c._lines_queue); ++i) {
            auto &line_info = c._lines_queue[i];
            line_info.chars_before_line = chars_before_line;
            chars_before_line += line_info.chars_in_line;
        }

        can_append_to_vbo = false;
    }

    // Prepare the new line's info.
    LineRange new_line_info;
    new_line_info.chars_in_line = num_chars;

    if (size(c._lines_queue) != 0) {
        const auto &last_line = c._lines_queue[size(c._lines_queue) - 1];
        new_line_info.chars_before_line = last_line.chars_in_line + last_line.chars_before_line;
    } else {
        new_line_info.chars_before_line = 0;
    }

    push_back(c._lines_queue, new_line_info);

    for (u32 i = 0; i < num_chars; ++i) {
        push_back(c._pager_quads_queue, char_quads_in_line[i]);
    }

    if (size(c._pager_quads_queue) > c._pager_verts_vbo_size) {
        c._pager_verts_vbo_size = size(c._pager_quads_queue);
        reallocate_pager_vbo(c);
        can_append_to_vbo = false;
    }

    glBindBuffer(GL_ARRAY_BUFFER, c._pager_verts_vbo);

    if (can_append_to_vbo) {
        CHECK_F(size(c._pager_quads_queue) <= c._pager_verts_vbo_size);

        auto queue_extent = get_extent(c._pager_quads_queue, quad_count_before_push, num_chars);

        glBufferSubData(GL_ARRAY_BUFFER,
                        quad_count_before_push * sizeof(eng::font::AlignedQuad),
                        queue_extent.first_chunk_size * sizeof(eng::font::AlignedQuad),
                        queue_extent.first_chunk);

        if (queue_extent.second_chunk_size != 0) {
            glBufferSubData(GL_ARRAY_BUFFER,
                            (quad_count_before_push + queue_extent.first_chunk_size) *
                                sizeof(eng::font::AlignedQuad),
                            queue_extent.second_chunk_size * sizeof(eng::font::AlignedQuad),
                            queue_extent.second_chunk);
        }
    } else {
        // Only reallocate the vbo if its size is not enough
        const u32 queue_size = size(c._pager_quads_queue);
        if (c._pager_verts_vbo_size < queue_size) {
            c._pager_verts_vbo_size = queue_size;
            reallocate_pager_vbo(c);
            LOG_F(INFO, "Reallocated pager vbo");
        }

        auto queue_extent = get_extent(c._pager_quads_queue, 0, size(c._pager_quads_queue));

        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        queue_extent.first_chunk_size * sizeof(eng::font::AlignedQuad),
                        queue_extent.first_chunk);

        if (queue_extent.second_chunk_size != 0) {
            glBufferSubData(GL_ARRAY_BUFFER,
                            queue_extent.first_chunk_size * sizeof(eng::font::AlignedQuad),
                            queue_extent.second_chunk_size * sizeof(eng::font::AlignedQuad),
                            queue_extent.second_chunk);
        }
    }

    // If the line ebo is not sufficient enough to draw this line with, allocate a larger ebo.
    if (c._pager_line_ebo_size < num_chars) {
        c._pager_line_ebo_size = std::max(num_chars, (u32)120);

        // 4 indices and a restart index for each quad
        std::vector<u16> quad_indices(5 * c._pager_line_ebo_size);

        u16 vertex_number = 0;

        for (u32 i = 0; i < quad_indices.size(); i += 5) {
            quad_indices[i] = vertex_number;
            quad_indices[i + 1] = vertex_number + 1;
            quad_indices[i + 2] = vertex_number + 2;
            quad_indices[i + 3] = vertex_number + 3;
            quad_indices[i + 4] = std::numeric_limits<u16>::max();
            vertex_number += 4;
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c._pager_line_verts_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     vec_bytes(quad_indices) + sizeof(eng::font::AlignedQuad),
                     nullptr,
                     GL_STATIC_DRAW);

        // ^^ + sizeof(eng::font::AlignedQuad) will let us store the pager aligned quad in there without
        // allocating yet another buffer.

        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, vec_bytes(quad_indices), quad_indices.data());

        PaddedRect pager_rect;
        pager_rect.set_topleft_including_padding(
            Vector2{0.0f, 0.0f}, Vector2{(f32)c._screen_width, (f32)c._pager_height}, 0.0f);

        eng::font::AlignedQuad pager_quad;
        pager_quad.set_from_padded_rect(pager_rect);

        pager_quad.vertices[eng::font::Corner::TOPLEFT].uv = {0.0f, 1.0f};
        pager_quad.vertices[eng::font::Corner::BOTLEFT].uv = {0.0f, 0.0f};
        pager_quad.vertices[eng::font::Corner::BOTRIGHT].uv = {1.0f, 0.0f};
        pager_quad.vertices[eng::font::Corner::TOPRIGHT].uv = {1.0f, 1.0f};

        glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER, vec_bytes(quad_indices), sizeof(eng::font::AlignedQuad), &pager_quad);
    }
}

void console_input(Console &c, const char *line, u32 num_chars) {
    auto &p = c._prompt;

    const u32 chars_before_adding = size(p._chars);

    const u32 num_chars_to_add = std::min(p._max_chars - chars_before_adding, num_chars);

    if (num_chars != num_chars_to_add) {
        LOG_F(WARNING, "Cannot add to prompt all the characters for line - %.*s", num_chars, line);
    }

    resize(p._chars, size(p._chars) + num_chars);
    std::copy(line, line + num_chars, begin(p._chars) + chars_before_adding);
    resize(p._aligned_quads, size(p._chars));

    TempAllocator512 ta(DEFAULT_ALLOCATOR);
    Array<i32> codepoints(ta, num_chars);
    std::copy(line, line + num_chars, begin(codepoints));

    // xyspoon - pad the end topleft
    eng::font::PushQuadReturn ret = eng::font::push_line_quads(c._font,
                                                               data(codepoints),
                                                               num_chars,
                                                               p._next_glyph_topleft.x,
                                                               (f32)c._screen_width,
                                                               0.f,
                                                               data(p._aligned_quads) + chars_before_adding);

    p._next_glyph_topleft.x = ret.next_topleft_x;
    p._next_glyph_topleft.y = 0.f;
    upload_prompt_vbo(c);

    // LOG_F(INFO, "p._next_glyph_topleft.x = %.2f", p._next_glyph_topleft.x);

    CHECK_F(size(p._aligned_quads) == size(p._chars));
}

void console_fmt_input(Console &c, const char *fmt, ...) {
    TempAllocator512 ta(DEFAULT_ALLOCATOR);
    Array<char> b(ta, 256);

    va_list l;
    va_start(l, fmt);
    vsnprintf(data(b), size(b), fmt, l);
    console_input(c, data(b), strlen(data(b)));
}

optional<std::string> console_input(Console &c, i32 codepoint) {
    auto &p = c._prompt;

    if (!(0 <= codepoint && codepoint <= 255)) {
        LOG_F(INFO, "Ignoring console input. Codepoint = %i", codepoint);
        return nullopt;
    }

    // On newline, add the line to pager, and reset the prompt.
    if (codepoint == '\n') {
        // LOG_F(INFO, "Adding chars in prompt to pager -");

        std::string string_in_prompt;
        string_in_prompt.reserve(size(p._chars));

        for (auto i = strlen(p._prompt_string); i < size(p._chars); ++i) {
            string_in_prompt += p._chars[i];
        }

        u32 prompt_string_length = strlen(p._prompt_string);

        const auto &aligned_quads = p._aligned_quads;

        add_line_to_pager(
            c, &data(aligned_quads)[prompt_string_length], size(aligned_quads) - prompt_string_length);

        reset_prompt(p);
        set_prompt_string(c);

        return string_in_prompt;
    }

    if ((i32)size(p._chars) >= p._max_chars) {
        // LOG_F(WARNING, "Prompt line too long");
        return nullopt;
    }

    char line[2] = "";
    line[0] = (char)codepoint;
    console_input(c, line, 1);

    return nullopt;
}

void console_backspace(Console &c) {
    if (size(c._prompt._chars) > strlen(c._prompt._prompt_string)) {
        c._prompt._next_glyph_topleft.x -= c._font.advance_width; // Works because fixed-width
        pop_back(c._prompt._aligned_quads);
        pop_back(c._prompt._chars);
        upload_prompt_vbo(c);
    }
}

void scroll_up_pager(Console &c) {
    ++c._scroll_offset_lines;
    c._scroll_offset_lines = std::min(c._max_scrollback_lines, c._scroll_offset_lines);
    c._pager_state_undrawn = true;
}

void scroll_down_pager(Console &c) {
    --c._scroll_offset_lines;
    c._scroll_offset_lines = std::max(0, c._scroll_offset_lines);
    c._pager_state_undrawn = true;
}

void add_string_to_pager(Console &c, const char *string, i32 length) {
    TempAllocator<8192> ta(DEFAULT_ALLOCATOR);
    Array<eng::font::AlignedQuad> quads(ta);

    // Extract lines from the string and push into pager

    if (length == -1) {
        length = strlen(string);
    }

    i32 i = 0;
    while (i < length) {
        while (i < length && string[i] == '\n') {
            ++i;
        }
        i32 j = std::min(i + 1, length);
        while (j < length && string[j] != '\n') {
            ++j;
        }
        if (j - i == 0) {
            break;
        }
        clear(quads);
        make_line_quads(c, &string[i], j - i, quads);
        add_line_to_pager(c, data(quads), size(quads));
        // LOG_F(INFO, "Adding line: %.*s", j - i, &string[i]);
        i = j + 1;
    }
}

// Pushes the given formatted string into the pager
void add_fmt_string_to_pager(Console &c, const char *fmt, ...) {
    TempAllocator512 ta(DEFAULT_ALLOCATOR);
    Array<char> b(ta, 256);

    va_list l;
    va_start(l, fmt);
    vsnprintf(data(b), size(b), fmt, l);
    add_string_to_pager(c, data(b), size(b));
}

void draw_prompt(Console &c) {
    auto &p = c._prompt;

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_UNIFORM_BUFFER, c._line_constants_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, c._line_constants_ubo_binding, c._line_constants_ubo);

    Vector2 min, max;
    p._box.get_inner_min_and_max(min, max);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Vector2), &min);

    glUseProgram(p._prompt_program_handle);

    glBindVertexArray(p._glyph_vertex_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexBuffer(0, p._prompt_verts_vbo, 0, sizeof(eng::font::GlyphQuadVertexData));

    // Draw each of the character quad, and then draw the cursor quad.
    const u32 num_char_quads = size(p._aligned_quads);

    for (u32 i = 0; i < num_char_quads; ++i) {
        glDrawArrays(GL_TRIANGLE_FAN, i * 4, 4);
    }

    glUseProgram(p._cursor_program_handle);
    glDrawArrays(GL_TRIANGLE_FAN, num_char_quads * 4, 4);
}

void draw_pager_updated(Console &c, GLuint current_draw_framebuffer) {
    if (!c._pager_state_undrawn) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);

    if (USE_TRANSPARENT_PAGER) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        // Two blend modes we can use to add up coverage, yielding the same result.
        // r_dest = r_source + r_dest * (1 - r_source) , or
        // r_dest = r_source * (1 - r_dest) + r_dest
        glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
    }

    c._pager_state_undrawn = false;

    // Bind pager fbo as target
    c._pager_fbo.bind_as_writable().set_draw_buffers({0});
    glViewport(0, 0, c._pager_width, c._pager_height);

    // Clear the color buffer.
    if (USE_TRANSPARENT_PAGER) {
        GLfloat clear_color[] = {0.2f, 0.2f, 0.2f, 0.7f};
        glClearBufferfv(GL_COLOR, 0, (const GLfloat *)&clear_color);
    } else {
        GLfloat clear_color[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, (const GLfloat *)&clear_color);
    }

    // Clear the depth buffer. (Don't need to do it actually since we are using GL_LEQUAL for text).
    f32 clear_depth = 1.0f;
    glClearBufferfv(GL_DEPTH, 0, &clear_depth);

    // Set shader and vao

    glUseProgram(c._pager_program_handle);
    glBindVertexArray(c._glyph_vertex_vao);

    glBindBuffer(GL_ARRAY_BUFFER, c._pager_verts_vbo);

    // Bind line offsets uniform
    glBindBuffer(GL_UNIFORM_BUFFER, c._line_constants_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, c._line_constants_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, c._line_constants_ubo_binding, c._line_constants_ubo);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c._pager_line_verts_ebo);

    if (USE_PRIMITIVE_RESTART) {
        glEnable(GL_PRIMITIVE_RESTART);
        glPrimitiveRestartIndex(std::numeric_limits<u16>::max());
    }

    // Each line is drawn one by one. xyspoon: draw it in one go?
    const i32 first_line_to_show =
        std::max(0, (i32)size(c._lines_queue) - (c._max_lines_showable + c._scroll_offset_lines));

    const i32 last_line_to_show =
        std::min(first_line_to_show + c._max_lines_showable, (i32)size(c._lines_queue));

    // Current line's offset, to be sourced into the line offset uniform
    Vector2 line_offset;
    line_offset.x = c._prompt._box.lowest.x;
    line_offset.y = 0.f;

    for (i32 line = first_line_to_show; line < last_line_to_show; ++line) {
        const auto &lr = c._lines_queue[line];

        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Vector2), &line_offset);

        glBindVertexBuffer(0,
                           c._pager_verts_vbo,
                           lr.chars_before_line * sizeof(eng::font::AlignedQuad),
                           sizeof(eng::font::GlyphQuadVertexData));

        if (USE_PRIMITIVE_RESTART) {
            glDrawElements(GL_TRIANGLE_FAN, lr.chars_in_line * 5, GL_UNSIGNED_SHORT, (const void *)0);
        } else {
            for (u32 i = 0; i < lr.chars_in_line; ++i) {
                glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, (const void *)(i * sizeof(u16) * 5));
            }
        }

        line_offset.y += (float)c._font.bbox_line_advance_pixels;
    }

    // Rebind the framebuffers prior to calling this function
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_draw_framebuffer);

    if (USE_PRIMITIVE_RESTART) {
        glDisable(GL_PRIMITIVE_RESTART);
    }
}

void blit_pager(Console &c) {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_DEPTH_TEST);

    glViewport(0, 0, c._screen_width, c._screen_height);

    c._pager_fbo.bind_as_readable(0).set_read_buffer(0);
    glUseProgram(c._blit_program_handle);

    // "Line offset" of 0, since we are using that same vertex shader for drawing lines.
    const Vector2 offset = {0.f, 0.f};
    glBindBuffer(GL_UNIFORM_BUFFER, c._line_constants_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, c._line_constants_ubo_binding, c._line_constants_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Vector2), &offset);

    // Using glyph vaos as it's the same format as pager quad
    glBindVertexArray(c._glyph_vertex_vao);

    glBindBuffer(GL_ARRAY_BUFFER, c._pager_line_verts_ebo);

    glBindVertexBuffer(0,
                       c._pager_line_verts_ebo,
                       c._pager_line_ebo_size * 5 * sizeof(u16),
                       sizeof(eng::font::GlyphQuadVertexData));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

} // namespace console
