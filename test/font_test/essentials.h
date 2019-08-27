/*

Example `glyph-files.txt`. Put the file in this very directory

minified_width = 16
minified_height = 16

glyph_png_dir = "/home/snyp/megasync/bmpfont"

atlas_width = 256
atlas_height = 256

*/

#pragma once

#include <glad/glad.h>

#include <assert.h>
#include <learnogl/app_loop.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/eye.h>
#include <learnogl/fps.h>
#include <learnogl/eng>
#include <learnogl/math_ops.h>
#include <learnogl/mesh.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/nf_simple.h>
#include <learnogl/rng.h>
#include <learnogl/stb_image.h>
#include <loguru.hpp>
#include <scaffold/array.h>
#include <scaffold/const_log.h>
#include <scaffold/math_types.h>
#include <scaffold/temp_allocator.h>

#include <array>
#include <variant>
#include <vector>

#include <learnogl/par_shapes.h>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

struct TextureFormat {
    GLint internal_format;
    GLenum external_format_components;
    GLenum external_format_component_type;
};

GLuint load_texture(const char *file_name, TextureFormat texture_format, GLenum texture_slot);

static inline GLuint create_vs_fs_prog(const char *vs_file, const char *fs_file) {
    using namespace fo;
    Array<uint8_t> vs_src(memory_globals::default_allocator());
    Array<uint8_t> fs_src(memory_globals::default_allocator());
    read_file(fs::path(vs_file), vs_src);
    read_file(fs::path(fs_file), fs_src);
    return eng::create_program((char *)data(vs_src), (char *)data(fs_src));
}

fo::AABB make_aabb(const fo::Vector3 *vertices, const uint16_t *indices, size_t num_indices,
                   fo::Matrix4x4 transform);

inline par_shapes_mesh *create_mesh() { return par_shapes_create_parametric_sphere(20, 20); }

constexpr fo::Matrix4x4 par_translate = math::translation_matrix(-0.5f, -0.5f, -0.5f);

constexpr int k_window_width = 800;
constexpr int k_window_height = 600;
constexpr float k_aspect_ratio = float(k_window_width) / k_window_height;
constexpr float Z_NEAR = 0.1f;
constexpr float Z_FAR = 1000.0f;
constexpr float HFOV = 70.0f;

static inline fo::Vector3 random_vector(float min, float max) {
    return fo::Vector3{(float)rng::random(min, max), (float)rng::random(min, max),
                       (float)rng::random(min, max)};
}

// par_shapes generated cube puts the center of the cube at [0.5, 0.5, 0.5],
// this will shift it to origin.
static inline void shift_par_cube(par_shapes_mesh *cube) {
    for (int i = 0; i < cube->npoints; ++i) {
        cube->points[i * 3] -= 0.5f;
        cube->points[i * 3 + 1] -= 0.5f;
        cube->points[i * 3 + 2] -= 0.5f;
    }
}

// Transforms a point from screen space to NDC space
static inline constexpr fo::Vector2 screen_to_ndc(const fo::Vector2 &screen_pos,
                                                  int screen_width = k_window_width,
                                                  int screen_height = k_window_height) {
    return fo::Vector2{2.0f * screen_pos.x / screen_width - 1.0f, 1.0f - 2.0f * screen_pos.y / screen_height};
}

// Transforms a vector from screen space to ndc space
static inline fo::Vector2 screen_to_ndc_vec(const fo::Vector2 &screen_vec, int screen_width = k_window_width,
                                            int screen_height = k_window_height) {
    return fo::Vector2{2.0f * screen_vec.x / screen_width, -2.0f * screen_vec.y / screen_height};
}

struct Quad {
    struct VertexData {
        fo::Vector3 position;
        fo::Vector2 st;
    };

    struct BindingStates {
        GLuint pos_attrib_location = 0;
        GLuint st_attrib_location = 1;
        GLuint pos_attrib_vbinding = 0;
        GLuint st_attrib_vbinding = 0;

        BindingStates() {}
    };

    std::vector<VertexData> vertices = std::vector<VertexData>(6);

    Quad() = default;

    Quad(fo::Vector2 min, fo::Vector2 max, float z = 0.4, const fo::Vector2 &bl = {0, 0},
         const fo::Vector2 &tl = {0, 1}, const fo::Vector2 &tr = {1, 1}, const fo::Vector2 &br = {1, 0});

    void make_vao(GLuint *vbo, GLuint *vao, BindingStates bs = BindingStates{});

    void make_with_ebo(GLuint *vbo, GLuint *ebo, GLuint *vao, BindingStates bs = BindingStates{});
};

// Generates the vertices of a quad given min and max.
void generate_quad_vertices(std::vector<fo::Vector2> &vertices, const fo::Vector2 &min,
                            const fo::Vector2 &max);

// Each position will have the given z component too. `min.z` should be equal to `max.z`.
void generate_quad_vertices(std::vector<fo::Vector3> &vertices, const fo::Vector3 &min,
                            const fo::Vector3 &max);

// Generates the indices of a quad.
void generate_quad_indices(std::vector<uint16_t> &indices);

// Fills given vector with the texcoords as given.
void generate_quad_texcoords(std::vector<fo::Vector2> &texcoords, const fo::Vector2 &bl = {0, 0},
                             const fo::Vector2 &tl = {0, 1}, const fo::Vector2 &tr = {1, 1},
                             const fo::Vector2 &br = {1, 0});

struct IVector2 {
    int32_t x, y;
};

inline constexpr IVector2 operator+(const IVector2 &a, const IVector2 &b) {
    return IVector2{a.x + b.x, a.y + b.y};
}
inline constexpr IVector2 operator-(const IVector2 &a, const IVector2 &b) {
    return IVector2{a.x - b.x, a.y - b.y};
}
inline constexpr IVector2 operator*(int32_t k, const IVector2 &v) { return IVector2{k * v.x, k * v.y}; }

inline constexpr IVector2 operator/(const IVector2 &v, int32_t k) { return IVector2{v.x / k, v.y / k}; }

inline constexpr IVector2 operator*(const IVector2 &a, const IVector2 &b) {
    return IVector2{a.x * b.x, a.y * b.y};
}

inline constexpr IVector2 clamp_ivec2(const IVector2 &min, const IVector2 &max, const IVector2 &value) {
    return IVector2{clamp(min.x, max.x, value.x), clamp(min.y, max.y, value.y)};
}

struct Rect2D {
    IVector2 min;
    IVector2 max; // Exclusive

    constexpr int32_t width() const { return max.x - min.x; }
    constexpr int32_t height() const { return max.y - min.y; }

    static constexpr Rect2D from_min_and_wh(const IVector2 &min, const IVector2 &wh) {
        return Rect2D{min, min + wh};
    }

    constexpr int32_t area() const { return width() * height(); }

    constexpr bool has_space() const { return area() > 0; }

    constexpr bool contains(const Rect2D &other) const {
        return min.x <= other.min.x && min.y <= other.min.y && max.x >= other.max.x && max.y >= other.max.y;
    }

    constexpr bool overlaps(const Rect2D &o) const {
        if (max.x <= o.min.x || min.x >= o.max.x) {
            return false;
        }
        if (max.y <= o.min.y || min.y >= o.max.y) {
            return false;
        }
        return true;
    }
};

#define RECT_XYXY(r) (r).min.x, (r).min.y, (r).max.x, (r).max.y

struct RectCornerExtent {
    fo::Vector2 botleft;
    fo::Vector2 extent;
};

struct Matrix3x3_Std140 {
    alignas(16) fo::Vector3 x = math::unit_x;
    alignas(16) fo::Vector3 y = math::unit_y;
    alignas(16) fo::Vector3 z = math::unit_z;

    Matrix3x3_Std140() = default;

    constexpr Matrix3x3_Std140(const fo::Vector3 &x, const fo::Vector3 &y, const fo::Vector3 &z)
        : x(x)
        , y(y)
        , z(z) {}

    constexpr Matrix3x3_Std140(const Matrix3x3_Std140 &o)
        : x(o.x)
        , y(o.y)
        , z(o.z) {}
};

inline Matrix3x3_Std140 m3_from_rect(const RectCornerExtent &r) {
    fo::Vector2 radius = {r.extent.x / 2.0f, r.extent.y / 2.0f};
    fo::Vector3 center = {r.botleft.x + radius.x, r.botleft.y + radius.y, 1.0f};
    return Matrix3x3_Std140{{radius.x / 2.0f, 0.0f, 0.0f}, {0.0f, radius.y / 2.0f, 0.0f}, center};
}

inline fo::Vector2 mul_m3_v2_vec(const Matrix3x3_Std140 &m, const fo::Vector2 &v) {
    using namespace math;
    fo::Vector3 v3 = {v.x, v.y, 0.0f};
    v3 = v3.x * m.x + v3.y * m.y;
    return fo::Vector2{v3.x, v3.y};
}

inline fo::Vector2 mul_m3_v2_pos(const Matrix3x3_Std140 &m, const fo::Vector2 &v) {
    using namespace math;
    fo::Vector3 v3 = {v.x, v.y, 0.0f};
    v3 = v3.x * m.x + v3.y * m.y + m.z;
    return fo::Vector2{v3.x, v3.y};
}

// Create a vbo and ebo from a par_shapes_mesh
void create_vbo_from_par_mesh(const par_shapes_mesh *mesh, GLuint *p_vbo, GLuint *p_ebo);

// The info about glyph. Collected while minimizing. Stored in a single file. Used while stitching together
// to make an atlas.
struct GlyphInfo {
    char c;
    IVector2 wh;        // Texture width and height
    char file_name[64]; // Only the file name
    float *data;        // The actual data as read from its own df file.

    // To be filled by `pack_rects`. Texture coords for the vertices of the glyph's quad.
    fo::Vector2 tl, bl, tr, br;
};

/// Pretty general blit function. Given two large 2D arrays pointerd to by `source_buffer` and `dest_buffer`,
/// rect_in_source and rect_in_dest denote some rectangle inside them, respectively. The pixels in
/// rect_in_source inside the source_buffer will be copied to dest_min_pos in the dest_buffer, and will be
/// clipped to rect_in_dest. We don't check that rect_in_source and rect_in_dest are themselves fully
/// contained in source_buffer_wh and dest_buffer_wh
template <typename T>
void blit_rect(const IVector2 &source_buffer_wh, const IVector2 &dest_buffer_wh, const Rect2D &source_rect,
               const Rect2D &dest_rect, const IVector2 &dest_min_pos, const T *source_buffer,
               T *dest_buffer) {
    // Calculate the target rectangle. Clipped to dest_rect's dimensions
    Rect2D target_rect;
    target_rect.min = clamp_ivec2(dest_rect.min, dest_rect.max, dest_min_pos);

    target_rect.max.x = target_rect.min.x + source_rect.width();
    target_rect.max.x = std::min(dest_rect.max.x, target_rect.max.x);
    target_rect.max.y = target_rect.min.y + source_rect.height();
    target_rect.max.y = std::min(dest_rect.max.y, target_rect.max.y);

    const int32_t row_width = target_rect.width();

    int32_t y = target_rect.min.y;
    int32_t source_y = source_rect.min.y;

    while (y < target_rect.max.y) {
        const int32_t target_row_start = y * dest_buffer_wh.x + target_rect.min.x;
        const int32_t source_row_start = source_y * source_buffer_wh.y + source_rect.min.x;
        memcpy(&dest_buffer[target_row_start], &source_buffer[source_row_start], row_width * sizeof(T));

        ++y;
        ++source_y;
    }
}

// Light wrap over vao functions.
struct Vao {
    GLuint _vao;

    Vao &gen() {
        glGenVertexArrays(1, &_vao);
        return *this;
    }

    const Vao &bind() const {
        glBindVertexArray(_vao);
        return *this;
    }

    const Vao &add_with_format(u32 attribute_number, u32 components, u32 component_type, bool normalized,
                               size_t relative_offset, u32 binding_point = 0) const {
        add_attrib(attribute_number, binding_point);
        set_format(attribute_number, components, component_type, normalized, relative_offset);
        return *this;
    }

    const Vao &add_attrib(u32 attribute_number, u32 binding_point = 0) const {
        glVertexAttribBinding((GLuint)attribute_number, binding_point);
        glEnableVertexAttribArray(attribute_number);
        return *this;
    }

    const Vao &set_format(u32 attribute_number, u32 components, GLenum component_type, bool normalized,
                          size_t relative_offset) const {
        glVertexAttribFormat((GLuint)attribute_number, (GLint)components, component_type,
                             normalized ? GL_TRUE : GL_FALSE, (GLuint)relative_offset);
        return *this;
    }

    constexpr operator GLuint() const { return _vao; }
};

const auto basic_quad_vs = R"(
    #version 430 core

    layout(location = 0) in vec2 pos_ndc;
    layout(location = 1) in vec2 st;

    struct PerQuadInfo {
        mat3 transform2D;
        vec4 color;
    };

    layout(binding = 0, std140) uniform QuadInfoBlock {
        PerQuadInfo arr[NUM_QUADS];
    } per_quad;

    uniform int quad_number;

    out VsOut {
        vec4 color;
    } vs_out;

    void main() {
        int i;

        if (quad_number < 0) {
            i = gl_InstanceID - quad_number;
        } else {
            i = quad_number;
        }

        mat3 transform2D = per_quad.arr[i].transform2D;
        vec4 color = per_quad.arr[i].color;

        vec3 corner = transform2D * vec3(pos_ndc, 1.0);
        gl_Position = vec4(corner, 1.0);

        vs_out.color = color;
    }
)";

const auto basic_quad_fs = R"(
    #version 430 core

    in VsOut {
        vec4 color;
    } fs_in;

    out vec4 frag_color;

    void main() {
        frag_color = fs_in.color;
    }
)";
