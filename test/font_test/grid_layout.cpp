#include "essentials.h"

using namespace fo;
using namespace math;

// In case no grid side is given in JSON file, this is a default
static constexpr int32_t default_grid_side = 20;

// Struct representing a rectangle
struct Rect2DFloat {
    Vector2 min;
    Vector2 max;

    bool contains_point(const Vector2 &p) const {
        return min.x <= p.x && p.x <= max.x && min.y <= p.y && p.y <= max.y;
    }
};

// Definition for each type of object we want to draw
namespace object_types {

constexpr size_t solid_rect_typeid = 0;

struct SolidRectangle {
    RGBA8 color; // Can allow transparency too
    Rect2DFloat rect;

    // Offset to the position buffer where this rectangle's vertices positions will be taken from
    size_t pos_attrib_offset;

    // Same as sbove but for the color buffer
    size_t color_attrib_offset;

    // Points to 4 Vector3 vertices comprising the quads. When depth changes on a click, we can quickly modify
    // the z of the position following this pointer.
    Vector3 *four_pos_pointer;
};

constexpr size_t image_typeid = 1;

// Represents a textured quad.
struct Image {
    GLuint texture; // Handle to the GL texture object
    Rect2DFloat rect;

    size_t pos_attrib_offset;

    Vector3 *four_pos_pointer;
};

// Represents a line
struct Line {
    Vector2 start;
    Vector2 end;
};

} // namespace object_types

using ObjectVariant = std::variant<object_types::SolidRectangle, object_types::Image, object_types::Line>;

// Collects pointers to the rectangle portion of a given object. So we don't have to use `switch` over and
// over.
struct QuadGeometry {
    Vector3 *four_pos_pointer;
    Vector2 *min;
    Vector2 *max;
};

QuadGeometry get_quad_geometry(ObjectVariant &object_variant) {
    QuadGeometry geom;

    switch (object_variant.index()) {
    case object_types::solid_rect_typeid: {
        auto &object = std::get<object_types::solid_rect_typeid>(object_variant);
        geom.four_pos_pointer = object.four_pos_pointer;
        geom.min = &object.rect.min;
        geom.max = &object.rect.max;
        break;
    }

    case object_types::image_typeid: {
        auto &object = std::get<object_types::image_typeid>(object_variant);
        geom.four_pos_pointer = object.four_pos_pointer;
        geom.min = &object.rect.min;
        geom.max = &object.rect.max;
        break;
    }
    }

    return geom;
}

// ---- Shaders

// Used to draw the lines
constexpr auto line_vs = R"(
    #version 430 core

    layout(location = 0) in vec2 position_screen;

    out VsOut {
        vec4 color;
        vec2 st;
    } vs_out;

    uniform mat4 proj_matrix;

    void main() {
        gl_Position = proj_matrix * vec4(position_screen, -1.0, 1.0);

        vs_out.color = vec4(0.0);
        vs_out.st = vec2(0.0);
    }
)";

// A fragment shader. The color_source uniform controls where the color comes from.
constexpr auto color_or_tex_fs = R"(
    #version 430 core

    // 0 means texture, 1 means fs_in.color
    uniform int color_source;

    uniform vec4 uniform_color;

    in VsOut {
        vec4 color;
        vec2 st;
    } fs_in;

    out vec4 frag_color;

    layout(binding = 0) uniform sampler2D image_sampler;

    void main() {
        if (color_source == 0) {
            frag_color = texture(image_sampler, fs_in.st).rgba;
        } else if (color_source == 1) {
            frag_color = fs_in.color;
        } else if (color_source == 2) {
            frag_color = uniform_color;
        } else {
            frag_color = vec4(1.0, 0.0, 1.0, 1.0);
        }
    }
)";

// Draws a quad on the screen. The texture coord `st` is only relevant when used for textured quads, and
// `color` is only relevant for solid colored quads.
constexpr auto quad_vs = R"(
    #version 430 core

    layout(location = 0) in vec3 position;
    layout(location = 1) in vec2 st;
    layout(location = 2) in vec4 color;

    uniform mat4 proj_matrix;

    out VsOut {
        vec4 color;
        vec2 st;
    } vs_out;

    void main() {
        gl_Position = proj_matrix * vec4(position, 1.0);

        vs_out.color = color;
        vs_out.st = st;
    }
)";

// Given a grid side length, clips the point to the nearest multiple of grid length. Returns the translation
// of the point.
static inline Vector2 snap_point_to_grid(Vector2 &point, float grid_side) {
    float x_prev = std::floor(point.x / grid_side) * grid_side;
    float x_next = x_prev + grid_side;
    float y_prev = std::floor(point.y / grid_side) * grid_side;
    float y_next = y_prev + grid_side;

    Vector2 points[] = {{x_prev, y_prev}, {x_prev, y_next}, {x_next, y_prev}, {x_next, y_next}};

    // Get the closest one
    int closest = 0;
    float dist = 9999999;

    for (int i = 0; i < 4; ++i) {
        Vector2 translation = points[i] - point;
        float newdist = (translation.x * translation.x) + (translation.y * translation.y);

        if (newdist < dist) {
            dist = newdist;
            closest = i;
        }
    }

    Vector2 translation = points[closest] - point;
    point = points[closest];
    return translation;
}

// The input states of the demo
namespace inputstates {

constexpr size_t hovering_id = 0;
struct Hovering {};

constexpr size_t down_on_quad_id = 1;
struct DownOnQuad {
    size_t index;
    QuadGeometry geom;
    Vector2 mouse_pos;

    bool just_clicked;
};

constexpr size_t dragging_quad_id = 2;
struct DraggingQuad {
    size_t index;
    QuadGeometry geom;
    Vector2 new_mouse_pos;
    Vector2 prev_mouse_pos;
};

} // namespace inputstates

using InputState = std::variant<inputstates::Hovering, inputstates::DownOnQuad, inputstates::DraggingQuad>;

// Everything the app works with
struct App {

    InputState cur_input_state = inputstates::Hovering{};

    int32_t window_height;
    int32_t window_width;
    RGBA8 background_color = RGBA8::from_hex(0xffffff);
    int32_t grid_side = default_grid_side;
    int32_t num_vertical_lines;
    int32_t num_horizontal_lines;

    std::vector<ObjectVariant> objects;

    // Objects are kept sorted in descending order of depth. So when we click on an object, we will test one
    // by one and see which one with the least depth contains the point.
    std::vector<size_t> depth_sorted_objects;

    // We keep the positions of the vertices. We re-upload to a vertex buffer when depth changes, or some
    // object is moved.
    std::vector<Vector3> all_positions;

    // Handles to GL objects.
    GLuint line_verts_vbo;
    GLuint line_format_vao;

    GLuint quad_vertex_ebo;
    GLuint quad_pos_vbo;
    GLuint quad_texcoords_vbo;
    GLuint quad_color_vbo;
    GLuint colored_quad_vao;
    GLuint textured_quad_vao;

    GLuint quad_params_ssbo;

    GLuint line_program;
    GLuint line_program_color_uloc;
    GLuint solid_quad_program;
    GLuint textured_quad_program;

    GLFWwindow *window;
    bool esc_pressed = false;

    const fs::path json_file;

    App(const char *json_file_)
        : json_file(json_file_) {}
};

// Parses JSON description
void parse_grid_description(App &app) {
    LOG_F(INFO, "Reading description file - %s", app.json_file.u8string().c_str());
    CHECK_F(fs::exists(app.json_file), "File doesn't exist");

    nfcd_ConfigData *cd = simple_parse_file(app.json_file.u8string().c_str(), true);

    auto root = nfcd_root(cd);

    auto grid_side = nfcd_object_lookup(cd, root, "grid_side");
    if (grid_side != NFCD_TYPE_NULL) {
        app.grid_side = (int32_t)nfcd_to_number(cd, grid_side);
    }

    auto size = SIMPLE_MUST(nfcd_object_lookup(cd, root, "size"));
    app.window_width = (int32_t)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, size, "w")));
    app.window_height = (int32_t)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, size, "h")));

    app.background_color = html_color(nfcd_to_string(
        cd, nfcd_object_lookup(cd, SIMPLE_MUST(nfcd_object_lookup(cd, root, "background")), "color")));

    LOG_F(INFO, "Creating screen of size = [%.i, %.i]", app.window_width, app.window_height);

    eng::start_gl(&app.window, app.window_width, app.window_height, 4, 4);
    eng::enable_debug_output(nullptr, nullptr);
    glfwSwapInterval(0);

    auto objects_loc = SIMPLE_MUST(nfcd_object_lookup(cd, root, "objects"));
    const int32_t num_objects = nfcd_array_size(cd, objects_loc);

    std::vector<ObjectVariant> objects;

    for (int32_t i = 0; i < num_objects; ++i) {
        app.depth_sorted_objects.push_back((size_t)i);

        Vector2 pos;

        auto item = nfcd_array_item(cd, objects_loc, i);
        auto position = SIMPLE_MUST(nfcd_object_lookup(cd, item, "position"));

        pos.x = (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, position, "x")));
        pos.y = (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, position, "y")));

        auto type = std::string(nfcd_to_string(cd, SIMPLE_MUST(nfcd_object_lookup(cd, item, "type"))));

        if (type == "rectangle") {
            objects.push_back(object_types::SolidRectangle{});

            auto &object = std::get<object_types::solid_rect_typeid>(objects.back());
            object.rect.min = pos;

            Vector2 wh;
            auto size = SIMPLE_MUST(nfcd_object_lookup(cd, item, "size"));
            wh.x = (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, size, "w")));
            wh.y = (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, size, "h")));
            object.rect.max = object.rect.min + wh;

            object.rect.min.y = app.window_height - object.rect.min.y;
            object.rect.max.y = app.window_height - object.rect.max.y;
            std::swap(object.rect.min.y, object.rect.max.y);

            object.color = html_color(nfcd_to_string(cd, SIMPLE_MUST(nfcd_object_lookup(cd, item, "color"))));

        } else if (type == "image") {
            objects.push_back(object_types::Image{});
            auto &object = std::get<object_types::image_typeid>(objects.back());

            object.rect.min = pos;

            Vector2 wh;
            auto size = SIMPLE_MUST(nfcd_object_lookup(cd, item, "size"));
            wh.x = (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, size, "w")));
            wh.y = (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, size, "h")));
            object.rect.max = object.rect.min + wh;

            object.rect.min.y = app.window_height - object.rect.min.y;
            object.rect.max.y = app.window_height - object.rect.max.y;
            std::swap(object.rect.min.y, object.rect.max.y);

            const char *url = nfcd_to_string(cd, SIMPLE_MUST(nfcd_object_lookup(cd, item, "url")));

            // Create a GL texture
            object.texture =
                load_texture(url, TextureFormat{GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE}, GL_TEXTURE0);
        } else {
            CHECK_F(false, "Invalid object type '%s'", type.c_str());
        }
    }

    app.objects.swap(objects);

    nfcd_free(cd);
}

// Loads all the shaders we need.
void load_shaders(App &app) {
    // The line drawing program
    auto vs = eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version,
                                     "#version 430 core\n", GL_VERTEX_SHADER, line_vs);

    auto fs = eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version,
                                     "#version 430 core\n", GL_FRAGMENT_SHADER, color_or_tex_fs);

    app.line_program = eng::create_program(vs, fs);
    app.line_program_color_uloc = glGetUniformLocation(app.line_program, "uniform_color");
    glUseProgram(app.line_program);
    glUniform1i(glGetUniformLocation(app.line_program, "color_source"), 2);

    // The solid quad drawing program
    vs = eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                                GL_VERTEX_SHADER, quad_vs);

    fs = eng::create_shader(eng::CreateShaderOptionBits::k_prepend_version, "#version 430 core\n",
                                GL_FRAGMENT_SHADER, color_or_tex_fs);

    app.solid_quad_program = eng::create_program(vs, fs);

    // Set the required uniforms for the program
    glUseProgram(app.solid_quad_program);
    glUniform1i(glGetUniformLocation(app.solid_quad_program, "color_source"), 1);

    // The textured quad drawing program
    app.textured_quad_program = eng::create_program(vs, fs);
    glUseProgram(app.textured_quad_program);
    glUniform1i(glGetUniformLocation(app.textured_quad_program, "color_source"), 0);
}

void draw_grid_lines(App &app) {
    glUseProgram(app.line_program);
    glUniform4f(app.line_program_color_uloc, 0.2f, 0.2f, 0.2f, 0.49f);

    glBindVertexArray(app.line_format_vao);
    glBindVertexBuffer(0, app.line_verts_vbo, 0, sizeof(Vector2));

    glDrawArrays(GL_LINES, 0, 2 * (app.num_vertical_lines + app.num_horizontal_lines));
}

// ----- Mouse and key callbacks

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    auto &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    // Not doing anything
}

void remake_depth_sorted_list(App &app) {
    std::sort(app.depth_sorted_objects.begin(), app.depth_sorted_objects.end(), [&app](size_t i, size_t j) {
        auto geom_i = get_quad_geometry(app.objects[i]);
        auto geom_j = get_quad_geometry(app.objects[j]);

        return geom_i.four_pos_pointer[0].z > geom_j.four_pos_pointer[0].z;
    });
}

// Information regarding the first quad that is under the clicked point
struct QuadUnderPoint {
    size_t index;
    size_t sorted_list_index;
    QuadGeometry geom;
};

void bring_quad_to_front(App &app, QuadUnderPoint &under) {
    app.depth_sorted_objects.erase(app.depth_sorted_objects.begin() + under.sorted_list_index);
    app.depth_sorted_objects.push_back(under.index);

    const int32_t num_objects = (int32_t)app.depth_sorted_objects.size();

    // Reassign depths
    for (int32_t i = num_objects - 1; i >= 0; --i) {
        float d = (float)(num_objects - 1 - i) / (num_objects - 1);

        size_t object_index = app.depth_sorted_objects[i];
        auto geom = get_quad_geometry(app.objects[object_index]);

        geom.four_pos_pointer[0].z = d;
        geom.four_pos_pointer[1].z = d;
        geom.four_pos_pointer[2].z = d;
        geom.four_pos_pointer[3].z = d;
    }
}

static inline Vector2 flip_point(int32_t window_height, float x, float y) {
    return Vector2{x, window_height - y};
}

static inline Vector2 get_mouse_pos(App &app) {
    double x, y;
    glfwGetCursorPos(app.window, &x, &y);
    return flip_point(app.window_height, (float)x, (float)y);
}

optional<QuadUnderPoint> get_quad_under_point(App &app, const Vector2 &point) {
    for (int32_t i = (int32_t)app.depth_sorted_objects.size() - 1; i >= 0; --i) {
        size_t object_index = app.depth_sorted_objects[i];
        auto g = get_quad_geometry(app.objects[object_index]);

        if (Rect2DFloat{*g.min, *g.max}.contains_point(point)) {
            return QuadUnderPoint{object_index, (size_t)i, g};
        }
    }
    return optional<QuadUnderPoint>{};
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    auto &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    Vector2 mouse_pos;

    if (action == GLFW_PRESS) {
        mouse_pos = get_mouse_pos(app);
    }

    switch (app.cur_input_state.index()) {

    case inputstates::hovering_id: {

        if (action == GLFW_PRESS && button == 0) {
            LOG_F(INFO, "Press");
            auto quad = get_quad_under_point(app, mouse_pos);

            if (quad) {
                auto &under_point = quad.value();
                LOG_F(INFO, "Quad under point: %zu", under_point.index);
                app.cur_input_state =
                    inputstates::DownOnQuad{under_point.index, under_point.geom, mouse_pos, true};

                bring_quad_to_front(app, under_point);
            }
        }

        break;
    }

    case inputstates::down_on_quad_id: {

        if (action == GLFW_PRESS) {
            auto &state = std::get<inputstates::down_on_quad_id>(app.cur_input_state);

            state.just_clicked = false;
            break;
        }

        if (action == GLFW_RELEASE) {
            app.cur_input_state = inputstates::Hovering{};
            break;
        }

        break;
    }

    case inputstates::dragging_quad_id: {

        if (action == GLFW_RELEASE) {
            app.cur_input_state = inputstates::Hovering{};
        }

        break;
    }
    }
}

static void mouse_movement_callback(GLFWwindow *window, double xpos, double ypos) {
    auto &app = *reinterpret_cast<App *>(glfwGetWindowUserPointer(window));

    switch (app.cur_input_state.index()) {

    case inputstates::hovering_id:
        break;

    case inputstates::down_on_quad_id: {
        Vector2 mouse_pos = get_mouse_pos(app);

        snap_point_to_grid(mouse_pos, app.grid_side);

        auto &state = std::get<inputstates::down_on_quad_id>(app.cur_input_state);

        app.cur_input_state = inputstates::DraggingQuad{state.index, state.geom, mouse_pos, state.mouse_pos};

        break;
    }

    case inputstates::dragging_quad_id: {
        Vector2 mouse_pos = get_mouse_pos(app);

        snap_point_to_grid(mouse_pos, app.grid_side);

        auto &state = std::get<inputstates::dragging_quad_id>(app.cur_input_state);

        app.cur_input_state =
            inputstates::DraggingQuad{state.index, state.geom, mouse_pos, state.new_mouse_pos};

        break;
    }
    }
}

// Initializes the vertex buffer for the grid's lines
void init_grid_vertex_buffer(App &app) {
    // Generate the grid line vertices
    app.num_vertical_lines = app.window_width / app.grid_side;
    app.num_horizontal_lines = app.window_height / app.grid_side;
    std::vector<Vector2> line_vertices;

    for (int32_t i = 0; i < app.num_vertical_lines * app.grid_side; i += app.grid_side) {
        Vector2 start = {(float)i, 0.0f};
        Vector2 end = {(float)i, (float)app.window_height};
        line_vertices.push_back(start);
        line_vertices.push_back(end);
    }

    for (int32_t i = 0; i < app.num_horizontal_lines * app.grid_side; i += app.grid_side) {
        Vector2 start = {0.0f, (float)i};
        Vector2 end = {(float)app.window_width, (float)i};

        line_vertices.push_back(start);
        line_vertices.push_back(end);
    }

    GLuint line_vbo, line_format_vao;
    glGenBuffers(1, &line_vbo);
    glGenVertexArrays(1, &line_format_vao);
    glBindVertexArray(line_format_vao);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(line_vertices), line_vertices.data(), GL_STATIC_DRAW);
    glBindVertexBuffer(0, line_vbo, 0, sizeof(Vector2));
    glVertexAttribBinding(0, 0); // 0 location, 0 binding
    glVertexAttribFormat(0, 2, GL_FLOAT, GL_FALSE, 0);
    glEnableVertexAttribArray(0);
    app.line_verts_vbo = line_vbo;
    app.line_format_vao = line_format_vao;
}

constexpr GLuint quad_pos_binding = 0;
constexpr GLuint quad_texcoord_binding = 1;
constexpr GLuint quad_color_binding = 2;

// Initializes the vertex buffers for positions, color, and textures
void init_quad_vertex_buffers(App &app) {
    // We create one vbo containing vertices of all quads, one vbo containing texture coords. Texture
    // coordinates are the same for all quads so textured quads can share the same buffer region.

    std::vector<RGBA8> all_colors;

    std::vector<Vector3> tmp_positions;

    // It's important to resize this as we will set up the depth points of each element into this vector.
    app.all_positions.resize(app.objects.size() * 4);

    // Fill up the position, texcoords, and color buffers as we examine each element.

    for (int32_t i = 0; i < app.objects.size(); ++i) {
        const Rect2DFloat *rect = nullptr;
        ObjectVariant &element = app.objects[i];

        Vector3 **p_four_pos_pointer = nullptr;

        switch (element.index()) {

        case object_types::solid_rect_typeid: {
            auto &object = std::get<object_types::solid_rect_typeid>(element);
            object.pos_attrib_offset = i * 4;
            object.color_attrib_offset = all_colors.size();

            // Same color for each of the 4 indices.
            all_colors.push_back(object.color);
            all_colors.push_back(object.color);
            all_colors.push_back(object.color);
            all_colors.push_back(object.color);

            rect = &object.rect;
            p_four_pos_pointer = &object.four_pos_pointer;

            break;
        }

        case object_types::image_typeid: {
            auto &object = std::get<object_types::image_typeid>(element);
            object.pos_attrib_offset = i * 4;

            rect = &object.rect;
            p_four_pos_pointer = &object.four_pos_pointer;

            break;
        }

        default:
            assert(0 && "UNPOSSIBLE");
        }

        float depth = (float)i / app.objects.size();

        generate_quad_vertices(tmp_positions, Vector3{rect->min.x, rect->min.y, depth},
                               Vector3{rect->max.x, rect->max.y, depth});

        std::copy(tmp_positions.begin(), tmp_positions.end(), app.all_positions.begin() + 4 * i);

        // Set up the pointer. Talk about ugliness.
        *p_four_pos_pointer = &app.all_positions[4 * i];
    }

    // Position buffer
    glGenBuffers(1, &app.quad_pos_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, app.quad_pos_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(app.all_positions), app.all_positions.data(),
                 GL_DYNAMIC_DRAW);

    // Texcoord buffer
    std::vector<Vector2> quad_texcoords;
    generate_quad_texcoords(quad_texcoords);

    glGenBuffers(1, &app.quad_texcoords_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, app.quad_texcoords_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(quad_texcoords), quad_texcoords.data(),
                 GL_STATIC_DRAW);

    // Color buffer
    glGenBuffers(1, &app.quad_color_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, app.quad_color_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(all_colors), all_colors.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &app.textured_quad_vao);
    glGenVertexArrays(1, &app.colored_quad_vao);

    // Generate an index buffer for both types of quads
    std::vector<uint16_t> quad_indices;
    generate_quad_indices(quad_indices);

    glGenBuffers(1, &app.quad_vertex_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.quad_vertex_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vec_bytes(quad_indices), quad_indices.data(),
                 GL_STATIC_DRAW);

    // Position format and indices are same for both types of quads
    for (GLuint vao : {app.textured_quad_vao, app.colored_quad_vao}) {
        glBindVertexArray(vao);

        glVertexAttribBinding(0, quad_pos_binding); // 0 location, 0 binding
        glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, 0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.quad_vertex_ebo);
    }

    // Texcoord is only for textured quads
    glBindVertexArray(app.textured_quad_vao);
    glVertexAttribBinding(1, quad_texcoord_binding); // 1 location, 1 binding
    glVertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, 0);
    glEnableVertexAttribArray(1);

    // Color is only for colored quads
    glBindVertexArray(app.colored_quad_vao);

    glVertexAttribBinding(2, quad_color_binding); // 2 location, 2 binding
    glVertexAttribFormat(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0);
    glEnableVertexAttribArray(2);
}

// ----- App loop

namespace app_loop {

template <> void render<App>(App &app); // Forward declaration

template <> void init<App>(App &app) {
    parse_grid_description(app);

    // glfwSetWindowSize(app.window, app.window_width, app.window_height);

    // Set up the perspective and orthographic projection matrices
    constexpr float near_z = 0.1f;
    constexpr float far_z = 1000.0f;

    // For 2D stuff, we need orthographic projection
    Matrix4x4 proj_matrix =
        orthographic_projection(0.0f, 0.0f, (float)app.window_width, (float)app.window_height);

    // Set up the wvp matrices ubo
    load_shaders(app);

    // Set the projection uniform
    glUseProgram(app.line_program);
    glUniformMatrix4fv(glGetUniformLocation(app.line_program, "proj_matrix"), 1, GL_FALSE,
                       (const float *)&proj_matrix);
    glUseProgram(app.solid_quad_program);
    glUniformMatrix4fv(glGetUniformLocation(app.solid_quad_program, "proj_matrix"), 1, GL_FALSE,
                       (const float *)&proj_matrix);
    glUseProgram(app.textured_quad_program);
    glUniformMatrix4fv(glGetUniformLocation(app.textured_quad_program, "proj_matrix"), 1, GL_FALSE,
                       (const float *)&proj_matrix);

    init_grid_vertex_buffer(app);
    init_quad_vertex_buffers(app);

    remake_depth_sorted_list(app);

    // Set the initial rendering states
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    glClearColor(app.background_color.r / 255.0f, app.background_color.g / 255.0f,
                 app.background_color.b / 255.0f, 1.0f);

    glfwSetWindowUserPointer(app.window, &app);
    glfwSetKeyCallback(app.window, key_callback);
    glfwSetMouseButtonCallback(app.window, mouse_button_callback);
    glfwSetCursorPosCallback(app.window, mouse_movement_callback);

    // Render once. Then wait for events.
    render(app);

    fps_title_update(app.window, 0);
}

template <> void update<App>(App &app, State &s) {
    glfwWaitEvents();

    // Check the current state of the app, and do the appropriate thing.
    switch (app.cur_input_state.index()) {

    case inputstates::hovering_id: {
        // LOG_F(INFO, "Hovering");

        break;
    }

    case inputstates::down_on_quad_id: {
        auto &state = std::get<inputstates::down_on_quad_id>(app.cur_input_state);

        if (state.just_clicked) {
            // Upload to GL buffer the new depths.
            glBindBuffer(GL_ARRAY_BUFFER, app.quad_pos_vbo);
            glInvalidateBufferData(app.quad_pos_vbo);
            glBufferData(GL_ARRAY_BUFFER, vec_bytes(app.all_positions), app.all_positions.data(),
                         GL_DYNAMIC_DRAW);
        }

        break;
    }

    case inputstates::dragging_quad_id: {
        auto &state = std::get<inputstates::dragging_quad_id>(app.cur_input_state);

        // Change the rectangle position of the quad that is currently being dragged and upload only this
        // one's updated positions to GL.

        auto &geom = state.geom;
        float depth = geom.four_pos_pointer[0].z;

        Vector2 translation = state.new_mouse_pos - state.prev_mouse_pos;

        *geom.min = *geom.min + translation;
        *geom.max = *geom.max + translation;

        translation = snap_point_to_grid(*geom.min, (float)app.grid_side);
        *geom.max = *geom.max + translation;

        std::vector<Vector3> new_vertices;
        generate_quad_vertices(new_vertices, Vector3{geom.min->x, geom.min->y, depth},
                               Vector3{geom.max->x, geom.max->y, depth});

        std::copy(new_vertices.begin(), new_vertices.end(), app.all_positions.begin() + state.index * 4);

        glBindBuffer(GL_ARRAY_BUFFER, app.quad_pos_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, state.index * 4 * sizeof(Vector3), 4 * sizeof(Vector3),
                        &app.all_positions[state.index * 4]);

        break;
    }
    }

    if (glfwGetKey(app.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.esc_pressed = true;
    }
}

template <> void render<App>(App &app) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw each object. One optimization would be to sort the objects depending on their type, as this would
    // reduce state changes in GL. Not doing that. One easy thing to do is to keep around the last state and
    // call GL function to change state only if we are drawing a different type of object.

    enum { STARTING_MODE, COLORED_QUAD_MODE, TEXTURED_QUAD_MODE };

    int current_mode = STARTING_MODE;

    for (auto &element : app.objects) {
        switch (element.index()) {
        case object_types::solid_rect_typeid: {
            auto &object = std::get<object_types::solid_rect_typeid>(element);

            if (current_mode != COLORED_QUAD_MODE) {
                current_mode = COLORED_QUAD_MODE;

                glUseProgram(app.solid_quad_program);
                glBindVertexArray(app.colored_quad_vao);
            }

            glBindVertexBuffer(quad_pos_binding, app.quad_pos_vbo, object.pos_attrib_offset * sizeof(Vector3),
                               sizeof(Vector3));

            glBindVertexBuffer(quad_color_binding, app.quad_color_vbo,
                               object.color_attrib_offset * sizeof(RGBA8), sizeof(RGBA8));

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            break;
        }

        case object_types::image_typeid: {
            auto &object = std::get<object_types::image_typeid>(element);

            if (current_mode != TEXTURED_QUAD_MODE) {
                current_mode = TEXTURED_QUAD_MODE;

                glUseProgram(app.textured_quad_program);
                glBindVertexArray(app.textured_quad_vao);

                glBindVertexBuffer(quad_texcoord_binding, app.quad_texcoords_vbo, 0, sizeof(Vector2));
            }

            glBindTexture(GL_TEXTURE_2D, object.texture);

            glBindVertexBuffer(quad_pos_binding, app.quad_pos_vbo, object.pos_attrib_offset * sizeof(Vector3),
                               sizeof(Vector3));

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            break;
        }
        }
    }

    draw_grid_lines(app);

    glfwSwapBuffers(app.window);
}

template <> bool should_close<App>(App &app) { return app.esc_pressed || glfwWindowShouldClose(app.window); }

template <> void close<App>(App &app) { eng::close_gl(); }

} // namespace app_loop

int main(int argc, char **argv) {
    memory_globals::init();
    fps_title_init("Grid layout assignment");

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <path_to_json_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    {
        app_loop::State loop_state{};
        App app(argv[1]);
        app_loop::run(app, loop_state);
    }
    memory_globals::shutdown();
}
