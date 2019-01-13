#pragma once

#include <glad/glad.h>
#include <learnogl/gl_binding_state.h>
#include <learnogl/kitchen_sink.h>
#include <scaffold/array.h>
#include <scaffold/math_types.h>

#include <learnogl/stb_rect_pack.h>

#include <learnogl/stb_truetype.h>

namespace eng {

// Representation of a font atlas, and functions to use them for drawing lines of text. See notes at the
// bottom of the file.
namespace font {

struct alignas(16) GlyphQuadVertexData {
    fo::Vector2 position_screen_space;
    fo::Vector2 uv;
};

// Representation of a glyph quad as 4 vertices. Can be drawn as TRIANGLE_FAN
struct AlignedQuad {
    GlyphQuadVertexData vertices[4];

    // Set vertex data from an stbtt_aligned_quad
    inline void set_from_stbquad(const stbtt_aligned_quad &q);

    // Set the vertex data from a padded rect
    inline void set_from_padded_rect(const PaddedRect &box);

    // Set the uv coodinates of the four corners. `uv_coords` points to an array of four uv coords.
    inline void set_uv(std::initializer_list<fo::Vector2> uv_coords);
};

// Same as above, but contains two triangles, so two vertices are duplicated. Easier to draw in one DrawArrays
// call with TRIANGLES primitive.
struct TwoTrianglesAlignedQuad {
    GlyphQuadVertexData vertices[6];

    inline void set_from_aligned_quad(const AlignedQuad &q);
};

static_assert(sizeof(AlignedQuad) == 4 * sizeof(GlyphQuadVertexData));

struct FontData {
    // Handle to atlas texture. Not valid anymore. Create the texture object yourself.
    GLuint atlas_texture_handle = 0;
    u32 texture_unit;

    fo::Array<u8> font_atlas;

    i32 atlas_width;
    i32 atlas_height;
    // Relevant font metrics
    i32 atlas_oversamples;
    f32 pixels_per_max_height;
    f32 pixels_per_ttu_height;
    i32 ascent_offset;
    f32 advance_width;            // Advance for fixed-width characters is same for all characters. In pixels.
    i32 bbox_line_advance_pixels; // How much the top-left corner has to advance in y (pixel space)

    fo::Array<stbtt_pack_range> _packed_ranges{ fo::memory_globals::default_allocator() };
    fo::Array<u8> _ttf_data{ fo::memory_globals::default_allocator() };

    stbtt_fontinfo _font_info;

    FontData() = default;
    FontData(const FontData &other) = delete;
    FontData(FontData &&other) = default;

    ~FontData();
};

// Denotes a contiguous range of codepoints
struct CodepointRange {
    int start;
    int size;
};

// Initialize a FontData structure. `pixels_per_max_height` is just the "font size". `check_if_in_range`
// denotes if we should check if we actually did bake the codepoint into the atlas. By default
// codepoint_ranges = nullptr and denotes the ascii characters.
void init(FontData &fd,
          BindingState &bs,
          const fs::path &ttf_file,
          u32 pixels_per_max_height,
          const CodepointRange *codepoint_ranges = nullptr,
          u32 num_ranges = 0,
          u32 oversamples = 1);

// Initialize font atlas. Keep the texture atlas in an array instead of creating a GL texture object.
void init(FontData &fd,
          const fs::path &ttf_file_path,
          u32 pixels_per_max_height,
          const CodepointRange *codepoint_ranges = nullptr,
          u32 num_ranges = 0,
          u32 oversamples = 1);

struct PushQuadReturn {
    u32 num_chars_pushed;
    u32 num_chars_in_string;
    f32 next_topleft_x;
};

// TODO: deprecate this function, use TwoTriangleAlignedQuads in console too. Given the string pushes the
// quads into the buffer pointed to by `quad_buffer`. If the 'current point' moves past `end_topleft_x`, we
// stop pushing quads. PushQuadReturn's `num_characters_put` field will tell how many characters of the given
// string could be pushed. `max_ascent_y` the y coordinate of the top side of a glyph having the maximum
// ascent. Suppose that the character 'L' has a maximum ascent. If I want L to just touch the top of the
// window, max_ascent_y would be 0.0f. See comment at the bottom of this file.
PushQuadReturn push_line_quads(FontData &fd,
                               const i32 *codepoints,
                               u32 count,
                               f32 start_topleft_x,
                               f32 end_topleft_x,
                               f32 max_ascent_y,
                               AlignedQuad *quad_buffer);

// As above. But pushes into buffer of TwoTriangleAlignedQuad.
PushQuadReturn push_line_quads(FontData &fd,
                               const i32 *codepoints,
                               u32 count,
                               f32 start_topleft_x,
                               f32 end_topleft_x,
                               f32 max_ascent_y,
                               TwoTrianglesAlignedQuad *quad_buffer);

PushQuadReturn push_single_quad(FontData &fd,
                                i32 codepoint,
                                f32 start_topleft_x,
                                f32 end_topleft_x,
                                f32 max_ascent_y,
                                TwoTrianglesAlignedQuad *out_p_quad);

std::string str(const FontData &fd);

} // namespace font

//  Impl of inlines

namespace font {

enum Corner : i32 {
    TOPLEFT = 0,
    BOTLEFT = 1,
    BOTRIGHT = 2,
    TOPRIGHT = 3,
};

void AlignedQuad::set_from_stbquad(const stbtt_aligned_quad &q) {
    vertices[TOPLEFT].position_screen_space.x = q.x0;
    vertices[TOPLEFT].position_screen_space.y = q.y0;
    vertices[BOTLEFT].position_screen_space.x = q.x0;
    vertices[BOTLEFT].position_screen_space.y = q.y1;
    vertices[BOTRIGHT].position_screen_space.x = q.x1;
    vertices[BOTRIGHT].position_screen_space.y = q.y1;
    vertices[TOPRIGHT].position_screen_space.x = q.x1;
    vertices[TOPRIGHT].position_screen_space.y = q.y0;

    vertices[TOPLEFT].uv.x = q.s0;
    vertices[TOPLEFT].uv.y = 1.0f - q.t0;
    vertices[BOTLEFT].uv.x = q.s0;
    vertices[BOTLEFT].uv.y = 1.0f - q.t1;
    vertices[BOTRIGHT].uv.x = q.s1;
    vertices[BOTRIGHT].uv.y = 1.0f - q.t1;
    vertices[TOPRIGHT].uv.x = q.s1;
    vertices[TOPRIGHT].uv.y = 1.0f - q.t0;
}

void AlignedQuad::set_from_padded_rect(const PaddedRect &box) {
    fo::Vector2 min, max;
    box.get_outer_min_and_max(min, max);

    vertices[TOPLEFT].position_screen_space.x = min.x;
    vertices[TOPLEFT].position_screen_space.y = min.y;
    vertices[BOTLEFT].position_screen_space.x = min.x;
    vertices[BOTLEFT].position_screen_space.y = max.y;
    vertices[BOTRIGHT].position_screen_space.x = max.x;
    vertices[BOTRIGHT].position_screen_space.y = max.y;
    vertices[TOPRIGHT].position_screen_space.x = max.x;
    vertices[TOPRIGHT].position_screen_space.y = min.y;

    // Not setting uvs.
}

void AlignedQuad::set_uv(std::initializer_list<fo::Vector2> uv_coords) {
    i32 i = 0;
    for (const fo::Vector2 &uv : uv_coords) {
        vertices[i].uv = uv;
        ++i;
    }
}

void TwoTrianglesAlignedQuad::set_from_aligned_quad(const AlignedQuad &q) {
    i32 i = 0;
    vertices[i++] = q.vertices[TOPLEFT];
    vertices[i++] = q.vertices[BOTLEFT];
    vertices[i++] = q.vertices[BOTRIGHT];
    vertices[i++] = q.vertices[TOPLEFT];
    vertices[i++] = q.vertices[BOTRIGHT];
    vertices[i++] = q.vertices[TOPRIGHT];
}

} // namespace font

} // namespace eng

// clang-format off
/*

Font metrics refresher.


              +---------------------+ Bounding box min corner.
             /
            v
            o-------------o  +
            |             |  ^
            |             |  |
            |             |  |  Max ascent. Positive in glyph space
            |             |  |
            |             |  v
       +--> o-------------o  <---------- y = 0, the baseline, in current glyph's local space. You need to specify y = what
       |    |             |  ^  Max descent. Negative in glyph space.                         to identity the baseline.
       |    o-------------o  v
       |
       +---------+  This is the point you give stbtt_GetPackedQuad, and it will return an stbtt_aligned_quad that
                    denotes the box along with texcoords. The aligned_quad will follow a top-down y increasing convention.
                    stbtt_GetPackedQuad will increase the x of the current point by the advance width of the glyph which is
                    constant for fixed width font. The y of the current point is not altered, since it is the baseline in
                    screen/whatever space you are in.

        In push_line_quads function, instead of the baseline's y, I take the topleft corner's y. The baseline's y is then simply
        corner.y + max_ascent.

        I also take the topleft corner x because you might want to append to a previous line of course.

*/

// clang-format on
