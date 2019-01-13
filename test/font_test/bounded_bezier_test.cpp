// Misnamed. Just a place for some 2D tests.

#include "distance_field.h"
#include "essentials.h"
#include <variant>
#include <unordered_map>

using namespace fo;
using namespace math;

constexpr int NUM_LINES = 30;
constexpr int NUM_CHARS_PER_LINE = 30;
constexpr int CELL_HEIGHT = k_window_height / NUM_LINES;
constexpr int CELL_WIDTH = k_window_width / NUM_CHARS_PER_LINE;
constexpr int NUM_CELLS = NUM_LINES * NUM_CHARS_PER_LINE;

// We store the essential data for each vertex of the editor in a vertex buffer.
struct CellVertex {
    // Position of the vertex (what coordinates is upto me later)
    Vector2 position;

    // ST coord into the glyph texture
    Vector2 st;
};

namespace inputstates {

constexpr int HOVERING = 0;
struct Hovering {};

constexpr int SELECT_IN_PROGRESS = 1;

struct SelectInProgress {
    Vector2 start; // Start corner in screen space
    Vector2 end;
    bool should_draw = false;
};

constexpr int SELECT_DONE = 2;

struct SelectDone {};

constexpr int CHARACTER_KEY_PRESS = 3;

struct CharacterKeyPress {
    char character;
};

}; // namespace inputstates

using InputState = std::variant<inputstates::Hovering, inputstates::SelectInProgress, inputstates::SelectDone,
                                inputstates::CharacterKeyPress>;

namespace click_responses {
struct Nothing {};
} // namespace click_responses

using ClickResponse = std::variant<click_responses::Nothing>;

namespace draw_primitives {
struct Nothing {};
} // namespace draw_primitives

enum class UniformLocation : unsigned {
    per_quad,
};

struct PerQuadInfo_Uniform {
    Matrix3x3_Std140 transform2D;
    Vector4 color;

    PerQuadInfo_Uniform() = default;
    PerQuadInfo_Uniform(const Matrix3x3_Std140 &transform2D, Vector4 color)
        : transform2D(transform2D)
        , color(color) {}
};

struct ButtonInfo {
    // Maybe don't need to store it here too
    uint32_t button_id;

    // I specify the the rectangle in NDC
    RectCornerExtent rect;

    // Plain single-colored buttons
    Vector4 color;

    ButtonInfo() = default;
    ButtonInfo(uint32_t button_id, RectCornerExtent rect, Vector4 color)
        : button_id(button_id)
        , rect(rect)
        , color(color) {}
};

struct PerEditorCell {
    Vector4 bl_tl;
    Vector4 tr_br;

    static inline PerEditorCell from_corners(Vector2 bl, Vector2 tl, Vector2 tr, Vector4 br) {
        return PerEditorCell{Vector4{bl.x, bl.y, tl.x, tl.y}, Vector4{tr.x, tr.y, br.x, br.y}};
    }
};

struct EditorCellChange {
    char new_char;
    IVector2 cell;
};

struct TexcoordUblock {
    Vector4 corner_st[2];
};

struct App {
    GLuint quad_vbo;
    GLuint quad_vao;
    GLuint quad_program;
    GLuint quad_number_loc;

    GLuint cell_vert_vao;
    GLuint cell_verts_vbo;
    // GLuint cell_quad_ebo;

    GLuint per_quad_ubo;

    // The host side uniform data for each quad is stored in this vector. Buttons and select quad.
    std::vector<PerQuadInfo_Uniform> per_quad_host_ub;

    // For the glyphs
    GLuint atlas_texture;

    // Program for drawing the cells in the editor. Only the VS is somewhat interesting.
    GLuint cell_draw_program;

    GLuint cell_texcoord_ubo;

    // Current editor cell. x = column, y = line
    IVector2 cur_cell = {-1, -1};

    // The cell which just changed. A char(-1) denotes that the character just got removed.
    std::optional<EditorCellChange> editor_cell_changed = {};

    std::unordered_map<char, TexcoordUblock> glyph_info;

    // Information for all buttons are stored in this list. Button ids are just indices of button in this
    // list.
    std::vector<ButtonInfo> button_infos;

    // Contains the indices of buttons that need to be redrawn
    std::vector<uint32_t> buttons_changed;

    InputState cur_input_state = inputstates::Hovering{};

    Matrix3x3_Std140 screen_to_ndc_mat;

    GLFWwindow *window;
    bool esc_pressed = false;
};

static ClickResponse click_activity(App &app, Vector2 mouse_screen_xy) { return click_responses::Nothing{}; }

void load_shaders(App &app) {
    const std::string num_quads_define =
        std::string("#define NUM_QUADS ") + std::to_string(app.per_quad_host_ub.size()) + "\n";

    auto vs =
        eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                               GL_VERTEX_SHADER, num_quads_define.c_str(), basic_quad_vs);

    auto fs =
        eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                               GL_FRAGMENT_SHADER, num_quads_define.c_str(), basic_quad_fs);

    app.quad_program = eng::create_program(vs, fs);
    app.quad_number_loc = glGetUniformLocation(app.quad_program, "quad_number");

    assert(app.per_quad_host_ub.size() != 0 && "Call load_glyphs first");

    const auto sd = fs::path(SOURCE_DIR);

    vs = eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                                GL_VERTEX_SHADER, sd / "glyph_draw.vert");

    fs = eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                                GL_FRAGMENT_SHADER, sd / "glyph_draw.frag");

    app.cell_draw_program = eng::create_program(vs, fs);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    auto &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    if (action == GLFW_PRESS) {
        char ch = -1;
        if (GLFW_KEY_A <= key && key <= GLFW_KEY_Z) {
            ch = 'A' + (char)(key - GLFW_KEY_A);
        }
        if (app.cur_input_state.index() == inputstates::HOVERING && ch != -1) {
            app.cur_input_state = inputstates::CharacterKeyPress{ch};
            LOG_F(INFO, "CharacterKeyPress event: %c", ch);
        }
    } else if (action == GLFW_REPEAT) {
        char ch = -1;
        if (GLFW_KEY_A <= key && key <= GLFW_KEY_Z) {
            ch = 'A' + (char)(key - GLFW_KEY_A);
        }
        if (app.cur_input_state.index() == inputstates::CHARACTER_KEY_PRESS) {
            app.cur_input_state = inputstates::CharacterKeyPress{ch};
        }
    } else if (action == GLFW_RELEASE) {
        if (app.cur_input_state.index() == inputstates::CHARACTER_KEY_PRESS) {
            app.cur_input_state = inputstates::Hovering{};
        }
    }
}

static inline Vector2 get_mouse_screen_pos(GLFWwindow *window) {
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    return Vector2{(float)x, (float)y};
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    auto &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    switch (app.cur_input_state.index()) {
    case inputstates::HOVERING:
        if (action == GLFW_PRESS && button == 0) {
            Vector2 click_pos = get_mouse_screen_pos(window);
            ClickResponse resp = click_activity(app, click_pos);
            if (resp.index() == 0) {
                app.cur_input_state = inputstates::SelectInProgress{click_pos, click_pos};
            }
        }

        break;
    case inputstates::SELECT_IN_PROGRESS:
        if (action == GLFW_RELEASE && button == 0) {
            app.cur_input_state = inputstates::SelectDone{};
        }

        break;
    case inputstates::SELECT_DONE:

        break;

    default:
        break;
    }
}

static void mouse_movement_callback(GLFWwindow *window, double xpos, double ypos) {
    auto &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    switch (app.cur_input_state.index()) {
    case inputstates::HOVERING: {
        break;
    }
    case inputstates::SELECT_IN_PROGRESS: {
        auto &select_info = std::get<1>(app.cur_input_state);
        float xf = (float)xpos;
        float yf = (float)ypos;

        if (xf != select_info.start.x && yf != select_info.start.y) {
            select_info.end = Vector2{xf, yf};
            select_info.should_draw = true;
        } else {
            select_info.should_draw = false;
        }
        break;
    }
    case inputstates::SELECT_DONE: {
        break;
    }

    default:
        break;
    }
}

// The vectors are in NDC
static inline void make_select_quad(Vector2 start, Vector2 end, Matrix3x3_Std140 &m) {
    m.x.x = std::abs(start.x - end.x) / 2.0f;
    m.y.y = std::abs(start.y - end.y) / 2.0f;
    m.z = Vector3{(start.x + end.x) / 2.0f, (start.y + end.y) / 2.0f, 1.0f};
}

static void make_buttons(App &a) {
    // Let's do a few buttons for whatever reason.
    RectCornerExtent button_rect = {screen_to_ndc({90, 90}), screen_to_ndc_vec({80, 40})};
    a.button_infos.emplace_back(a.button_infos.size(), button_rect, Vector4{0.84f, 0.2f, 0.60f, 1.0f});

    button_rect = {screen_to_ndc({200, 90}), screen_to_ndc_vec({80, 40})};
    a.button_infos.emplace_back(a.button_infos.size(), button_rect, Vector4{0.84f, 0.2f, 0.60f, 1.0f});
}

static void make_per_quad_ubo(App &app) {
    // First goes the select quad
    app.per_quad_host_ub.reserve(1 + app.button_infos.size());
    app.per_quad_host_ub.emplace_back(Matrix3x3_Std140{}, Vector4{0.6f, 0.6f, 0.8f, 0.4f});

    // Then goes each button
    for (size_t i = 0; i < app.button_infos.size(); ++i) {
        Matrix3x3_Std140 transform2D = m3_from_rect(app.button_infos[i].rect);
        app.per_quad_host_ub.emplace_back(transform2D, app.button_infos[i].color);
    }

    // Make the ubo
    glGenBuffers(1, &app.per_quad_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, app.per_quad_ubo);
    glBufferData(GL_UNIFORM_BUFFER, vec_bytes(app.per_quad_host_ub), app.per_quad_host_ub.data(),
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, app.per_quad_ubo);
}

static void load_glyphs(App &app) {
    nfcd_ConfigData *cd =
        simple_parse_file((fs::path(SOURCE_DIR) / "glyph-files.txt").u8string().c_str(), true);

    DEFER([cd]() { nfcd_free(cd); });

    auto r = nfcd_root(cd);
    fs::path glyph_png_dir = nfcd_to_string(cd, nfcd_object_lookup(cd, r, "glyph_png_dir"));

    const auto glyph_infos_file = glyph_png_dir / "glyph_infos.data";

    const auto atlas_output_path =
        nfcd_to_string(cd, SIMPLE_MUST(nfcd_object_lookup(cd, r, "atlas_output_path")));

    DistanceField df_atlas = DistanceField::from_df_file(atlas_output_path);

    // Upload the atlas. Not using multiple mips, because this is just a test dammit x)
    glGenTextures(1, &app.atlas_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app.atlas_texture);

    flip_2d_array_vertically((uint8_t *)df_atlas.v.data(), sizeof(float), df_atlas.w, df_atlas.h);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, df_atlas.w, df_atlas.h, 0, GL_RED, GL_FLOAT, df_atlas.v.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    auto glyph_infos = read_structs_into_vector<GlyphInfo>(glyph_infos_file);

    LOG_SCOPE_F(INFO, "Loading glyph data");
    for (auto &info : glyph_infos) {
        app.glyph_info.insert(std::make_pair(info.c, TexcoordUblock{}));

        auto &tc = app.glyph_info.at(info.c);
        tc.corner_st[0].x = info.tl.x;
        tc.corner_st[0].y = info.tl.y;
        tc.corner_st[0].z = info.bl.x;
        tc.corner_st[0].w = info.bl.y;
        tc.corner_st[1].x = info.tr.x;
        tc.corner_st[1].y = info.tr.y;
        tc.corner_st[1].z = info.br.x;
        tc.corner_st[1].w = info.br.y;

        LOG_F(INFO, "Loaded glyph: %c", info.c);
    }
}

namespace app_loop {

template <> void render<App>(App &app);

template <> void init<App>(App &app) {
    eng::start_gl(&app.window, k_window_width, k_window_height, 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    // For responsive ui, turning off vsync is a good idea.
    glfwSwapInterval(0);

    make_buttons(app);

    Quad quad{{-1.0f, -1.0f}, {1.0f, 1.0f}};

    auto bs = Quad::BindingStates();
    bs.pos_attrib_location = 0;
    bs.st_attrib_location = 1;
    bs.pos_attrib_vbinding = 0;
    bs.st_attrib_vbinding = 1;

    quad.make_vao(&app.quad_vbo, &app.quad_vao, bs);

    make_per_quad_ubo(app);

    load_glyphs(app);

    load_shaders(app);

    // Load the glyph texture

    // We use another vao for drawing cell quads.

    GLuint cell_verts_vbo;
    glGenBuffers(1, &cell_verts_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, cell_verts_vbo);

    // Allocate data for each vertex. There are 4 vertices per cell which we draw with GL_TRIANGLE_STRIP
    // primitive.
    glBufferData(GL_ARRAY_BUFFER, NUM_CELLS * 4 * sizeof(CellVertex), nullptr, GL_DYNAMIC_DRAW);

    // Initializing each cell's vertex data
    auto cell_data = (CellVertex *)glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);

    // The screen to ndc transform
    app.screen_to_ndc_mat.x = Vector3{2.0f / k_window_width, 0.0f, 0.0f};
    app.screen_to_ndc_mat.y = Vector3{0.0f, -2.0f / k_window_height, 0.0f};
    app.screen_to_ndc_mat.z = Vector3{-1.0f, 1.0f, 1.0f};

    const auto to_ndc = [&app](float x, float y, float xadd, float yadd) {
        Vector2 pos = {x * CELL_WIDTH + xadd, y * CELL_HEIGHT + yadd};
        pos = mul_m3_v2_pos(app.screen_to_ndc_mat, pos);
        pos.x = clamp(-1.0f, 1.0f, pos.x);
        pos.y = clamp(-1.0f, 1.0f, pos.y);
        return pos;
    };

    for (int y = 0; y < NUM_LINES; ++y) {
        for (int x = 0; x < NUM_CHARS_PER_LINE; ++x) {
            const int cell_number = y * NUM_CHARS_PER_LINE + x;
            CellVertex *v = &cell_data[4 * cell_number];

            constexpr float gap = 0.0f;

            // TL
            v[0].position = to_ndc(float(x), float(y), gap, gap);
            // BL
            v[1].position = to_ndc(float(x), float(y + 1), gap, -gap);
            // TR
            v[2].position = to_ndc(float(x + 1), float(y), -gap, gap);
            // BR
            v[3].position = to_ndc(float(x + 1), float(y + 1), -gap, -gap);

            v[0].position = Vector2{-1.0f, 1.0f};
            v[1].position = Vector2{-1.0f, -1.0f};
            v[2].position = Vector2{1.0f, 1.0f};
            v[3].position = Vector2{1.0f, -1.0f};

            v[0].st = v[1].st = v[2].st = v[3].st = Vector2{};

#define XY(v) (v).x, (v).y

#if 0
            fprintf(stdout, R"(
                Cell: %i
                0 = [%.2f, %.2f]
                1 = [%.2f, %.2f]
                2 = [%.2f, %.2f]
                3 = [%.2f, %.2f]
            )", cell_number, XY(v[0].position), XY(v[1].position), XY(v[2].position),
                    XY(v[3].position));
#endif
        }
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);

    // Texture coordinate ubo for a cell's 4 vertices
    app.cell_texcoord_ubo = create_uniform_buffer(1, sizeof(TexcoordUblock));
    glBindBuffer(GL_UNIFORM_BUFFER, app.cell_texcoord_ubo);

#if 0
    // We draw the cell quads indexed. So need an ebo
    GLuint cell_quad_ebo;
    glGenBuffers(1, &cell_quad_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cell_quad_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(uint16_t), nullptr, GL_STATIC_DRAW);
    uint16_t *index = (uint16_t *)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_MAP_WRITE_BIT);

    constexpr int BL = 0, TL = 1, TR = 2, BR = 3;

    index[0] = BL;
    index[1] = TR;
    index[2] = TL;
    index[3] = BL;
    index[4] = BR;
    index[5] = TR;

    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
#endif

    // The vertex format for the cell quads is expressed in a vao
    GLuint cell_vert_vao;
    glGenVertexArrays(1, &cell_vert_vao);

    glBindVertexArray(cell_vert_vao);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glVertexAttribFormat(0, 2, GL_FLOAT, GL_FALSE, offsetof(CellVertex, position));
    glVertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, offsetof(CellVertex, st));

    glVertexAttribBinding(0, 0);
    glVertexAttribBinding(1, 0);

    // app.cell_quad_ebo = cell_quad_ebo;
    app.cell_verts_vbo = cell_verts_vbo;
    app.cell_vert_vao = cell_vert_vao;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetKeyCallback(app.window, key_callback);
    glfwSetMouseButtonCallback(app.window, mouse_button_callback);
    glfwSetCursorPosCallback(app.window, mouse_movement_callback);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glfwSwapBuffers(app.window);
}

template <> void update<App>(App &app, State &s) {
    if (app.cur_input_state.index() == inputstates::SELECT_DONE) {
        app.cur_input_state = inputstates::Hovering{};
        glfwPostEmptyEvent();
    }

    if (app.editor_cell_changed) {
        app.editor_cell_changed = nullopt;
    }

    glfwWaitEvents();

    if (app.cur_input_state.index() == inputstates::CHARACTER_KEY_PRESS) {
        auto ch = std::get<inputstates::CHARACTER_KEY_PRESS>(app.cur_input_state).character;

        app.cur_cell.x = (app.cur_cell.x + 1) % NUM_CHARS_PER_LINE;
        if (app.cur_cell.x == 0) {
            ++app.cur_cell.y;
        }

        debug("Current cell = [%i, %i]", app.cur_cell.x, app.cur_cell.y);
        app.editor_cell_changed = EditorCellChange{ch};
    }

    else if (app.cur_input_state.index() == inputstates::SELECT_IN_PROGRESS) {
        // Need to draw a select quad
        auto &select_info = std::get<inputstates::SELECT_IN_PROGRESS>(app.cur_input_state);
        make_select_quad(screen_to_ndc(select_info.start), screen_to_ndc(select_info.end),
                         app.per_quad_host_ub[0].transform2D);

        glBindBuffer(GL_UNIFORM_BUFFER, app.per_quad_ubo);
        glInvalidateBufferData(app.per_quad_ubo);
        glBufferData(GL_UNIFORM_BUFFER, vec_bytes(app.per_quad_host_ub),
                     app.per_quad_host_ub.data(), GL_DYNAMIC_DRAW);
    }

    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    const auto cur_input_state = app.cur_input_state.index();

    bool should_draw_select = false;

    if (cur_input_state == inputstates::SELECT_IN_PROGRESS) {
        should_draw_select = std::get<inputstates::SELECT_IN_PROGRESS>(app.cur_input_state).should_draw;
    } else if (cur_input_state == inputstates::SELECT_DONE) {
        should_draw_select = true;
    }

    // LOG_F(INFO, "CLEAR COLOR");
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw buttons
    // glUniform1i(app.quad_number_loc, -1);
    // glDrawArraysInstanced(GL_TRIANGLES, 0, 6, app.button_infos.size());

    // Draw select quad
    if (should_draw_select) {
        glUseProgram(app.quad_program);

        LOG_F(INFO, "Drawing select quad");
        glUniform1i(app.quad_number_loc, 0);
        glBindVertexArray(app.quad_vao);

        glBindVertexBuffer(0, app.quad_vbo, 0, sizeof(Quad::VertexData));

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

        // Draw editor cell that changed

#if 1
    if (app.editor_cell_changed) {
        LOG_F(INFO, "Drawing changed cell");

        glUseProgram(app.cell_draw_program);

        glBindVertexArray(app.cell_vert_vao);

        auto cell_vb_offset = (app.cur_cell.y * NUM_CHARS_PER_LINE + app.cur_cell.x) * 4 * sizeof(CellVertex);

        auto glyph_info = app.glyph_info.find(app.editor_cell_changed.value().new_char);
        if (glyph_info == app.glyph_info.end()) {
            LOG_F(ERROR, "Unknown glyph: %c", app.editor_cell_changed.value().new_char);
            return;
        }

        auto &corner_st = glyph_info->second.corner_st;

        glBindBuffer(GL_UNIFORM_BUFFER, app.cell_texcoord_ubo);

        // LOG_F(INFO, "Corner st = [%f, %f], [%f, %f], [%f, %f], [%f, %f]", XYZW(corner_st[0]), XYZW(corner_st[1]));

        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(TexcoordUblock), &corner_st);

        glBindBuffer(GL_ARRAY_BUFFER, app.cell_verts_vbo);
        glBindVertexBuffer(0, app.cell_verts_vbo, cell_vb_offset, sizeof(CellVertex));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        app.editor_cell_changed = nullopt;
    }
#else

    const auto draw_cell = [&](IVector2 cell) {
        LOG_F(INFO, "Drawing char");

        // auto cell_vb_offset = (cell.y * NUM_CHARS_PER_LINE + cell.x) * 4 * sizeof(CellVertex);
        // size_t cell_vb_offset = 0;

        // glBindVertexBuffer(0, app.cell_verts_vbo, cell_vb_offset, sizeof(CellVertex));
        glBindVertexBuffer(0, app.cell_verts_vbo, 0, sizeof(CellVertex));

        glDrawArrays(GL_TRIANGLE_STRIP, (cell.y * NUM_CHARS_PER_LINE + cell.x) * 4, 4);

        app.editor_cell_changed = nullopt;
    };

    if (app.editor_cell_changed) {
        glUseProgram(app.cell_draw_program);
        glBindVertexArray(app.cell_vert_vao);
        glBindBuffer(GL_ARRAY_BUFFER, app.cell_verts_vbo);

        draw_cell({0, 0});
        draw_cell({15, 15});
        draw_cell({20, 20});
        draw_cell({10, 20});
        draw_cell({12, 21});
        draw_cell({19, 29});
    }
#endif
    glfwSwapBuffers(app.window);
}

template <> bool should_close<App>(App &app) { return app.esc_pressed || glfwWindowShouldClose(app.window); }

template <> void close<App>(App &app) { eng::close_gl(); }

} // namespace app_loop

int main() {
    memory_globals::init();
    rng::init_rng();
    // fps_title_init("Bezier curve editing");
    {
        App app;
        app_loop::State s{};
        app_loop::run(app, s);
    }
    memory_globals::shutdown();
}
