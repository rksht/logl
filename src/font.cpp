#include <learnogl/essential_headers.h>

#include <fmt/format.h>
#include <learnogl/font.h>
#include <learnogl/gl_binding_state.h>
#include <learnogl/stb_image_write.h>

using namespace fo;

namespace eng {

namespace font {

void init_without_texture(FontData &fd,
                          const fs::path &ttf_file,
                          u32 pixels_per_max_height,
                          const CodepointRange *codepoint_ranges = nullptr,
                          u32 num_ranges = 0,
                          u32 oversamples = 1) {
    read_file(ttf_file, fd._ttf_data, false);
    stbtt_InitFont(&fd._font_info, data(fd._ttf_data), 0);

    i32 max_ascent_ttu, max_descent_ttu, line_gap_ttu;
    stbtt_GetFontVMetrics(&fd._font_info, &max_ascent_ttu, &max_descent_ttu, &line_gap_ttu);

    // As mentioned, only makes sense for fixed-width fonts
    i32 advance_width, left_side_bearing;
    stbtt_GetCodepointHMetrics(&fd._font_info, 'A', &advance_width, &left_side_bearing);

    fd.atlas_width = 1024;
    fd.atlas_height = 1024;
    fd.atlas_oversamples = oversamples;
    fd.pixels_per_max_height = (f32)pixels_per_max_height;
    fd.pixels_per_ttu_height = stbtt_ScaleForPixelHeight(&fd._font_info, fd.pixels_per_max_height);
    fd.ascent_offset = (int)std::ceil(max_ascent_ttu * fd.pixels_per_ttu_height);
    fd.advance_width = advance_width * fd.pixels_per_ttu_height;

    // How many pixels to move down y per line
    fd.bbox_line_advance_pixels =
        (i32)std::ceil((max_ascent_ttu - max_descent_ttu + line_gap_ttu) * fd.pixels_per_ttu_height);

    // Allocate storage for the atlas bitmap
    fo::resize(fd.font_atlas, fd.atlas_width * fd.atlas_height);

    static const CodepointRange ascii_range = { 0, 128 };

    if (codepoint_ranges == nullptr) {
        DCHECK_EQ_F(num_ranges, 0u);
        codepoint_ranges = &ascii_range;
        num_ranges = 1;
    }

    // Allocate storage for packed char info and pack the ranges
    resize(fd._packed_ranges, num_ranges);
    memset(data(fd._packed_ranges), 0, num_ranges);

    for (u32 i = 0; i < num_ranges; ++i) {
        stbtt_pack_range &r = fd._packed_ranges[i];
        r.font_size = fd.pixels_per_max_height;
        r.first_unicode_codepoint_in_range = codepoint_ranges[i].start;
        r.num_chars = codepoint_ranges[i].size;
        r.chardata_for_range = reinterpret_cast<stbtt_packedchar *>(
            memory_globals::default_allocator().allocate(r.num_chars * sizeof(stbtt_packedchar)));
    }

    stbtt_pack_context pack_context;

    i32 res =
        stbtt_PackBegin(&pack_context, data(fd.font_atlas), fd.atlas_width, fd.atlas_height, 0, 1, nullptr);

    CHECK_F(res != 0, "FontData::init - Failed stbtt_PackBegin");
    stbtt_PackSetOversampling(&pack_context, fd.atlas_oversamples, fd.atlas_oversamples);

    res = stbtt_PackFontRanges(
        &pack_context, data(fd._ttf_data), 0, data(fd._packed_ranges), size(fd._packed_ranges));

    CHECK_F(res != 0, "FontData::init - Failed stbtt_PackFontRanges");

    stbtt_PackEnd(&pack_context);

    // Create the texture object
    flip_2d_array_vertically(data(fd.font_atlas), sizeof(u8), fd.atlas_width, fd.atlas_height);
}

void init(FontData &fd,
          BindingState &bs,
          const fs::path &ttf_file,
          u32 pixels_per_max_height,
          const CodepointRange *codepoint_ranges,
          u32 num_ranges,
          u32 oversamples) {

    init_without_texture(fd, ttf_file, pixels_per_max_height, codepoint_ranges, num_ranges, oversamples);

    glGenTextures(1, &fd.atlas_texture_handle);

    fd.texture_unit = bs.bind_unique(gl_desc::SampledTexture(fd.atlas_texture_handle));

    glBindTexture(GL_TEXTURE_2D, fd.atlas_texture_handle);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_R8,
                 fd.atlas_width,
                 fd.atlas_height,
                 0,
                 GL_RED,
                 GL_UNSIGNED_BYTE,
                 data(fd.font_atlas));

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    LOG_F(INFO, "Initialized font data");
}

void init(FontData &fd,
          const fs::path &ttf_file,
          u32 pixels_per_max_height,
          const CodepointRange *codepoint_ranges,
          u32 num_ranges,
          u32 oversamples) {
    init_without_texture(fd, ttf_file, pixels_per_max_height, codepoint_ranges, num_ranges, oversamples);
}

FontData::~FontData() {
    for (stbtt_pack_range &r : _packed_ranges) {
        memory_globals::default_allocator().deallocate(r.chardata_for_range);
    }
}

PushQuadReturn push_line_quads(FontData &fd,
                               const i32 *codepoints,
                               u32 count,
                               f32 start_topleft_x,
                               f32 end_topleft_x,
                               f32 max_ascent_y,
                               AlignedQuad *quad_buffer) {
    PushQuadReturn ret;
    ret.num_chars_in_string = count;
    ret.num_chars_pushed = 0;

    Vector2 next_topleft = { start_topleft_x, max_ascent_y };
    ret.next_topleft_x = start_topleft_x;

    Vector2 base = next_topleft;
    base.y += fd.ascent_offset;

    for (u32 i = 0; i < count; ++i) {
        i32 codepoint = codepoints[i];

        // Find range
        u32 range_number = 0;
        for (auto &r : fd._packed_ranges) {
            if (r.first_unicode_codepoint_in_range <= codepoint &&
                codepoint < r.first_unicode_codepoint_in_range + r.num_chars) {
                break;
            }
            ++range_number;
        }

        // Not found
        if (range_number == size(fd._packed_ranges)) {
            LOG_F(WARNING, "Codepoint: %i not in packed unicode ranges", codepoint);
            return ret;
        }

        // Get the quad
        stbtt_packedchar *packed_chars = fd._packed_ranges[range_number].chardata_for_range;

        stbtt_aligned_quad glyph_aligned_quad;
        stbtt_GetPackedQuad(packed_chars,
                            fd.atlas_width,
                            fd.atlas_height,
                            codepoint,
                            &base.x,
                            &base.y,
                            &glyph_aligned_quad,
                            0);

        next_topleft.x = base.x;

        // Check if we are exceeding the end_topleft_x
        if (next_topleft.x >= end_topleft_x) {
            return ret;
        }

        // If not, just push the quad and continue
        ret.next_topleft_x = next_topleft.x;
        ++ret.num_chars_pushed;
        quad_buffer[i].set_from_stbquad(glyph_aligned_quad);
    }

    return ret;
}

PushQuadReturn push_line_quads(FontData &fd,
                               const i32 *codepoints,
                               u32 count,
                               f32 start_topleft_x,
                               f32 end_topleft_x,
                               f32 max_ascent_y,
                               TwoTrianglesAlignedQuad *quad_buffer) {

    PushQuadReturn ret;
    ret.num_chars_in_string = count;
    ret.num_chars_pushed = 0;

    Vector2 next_topleft = { start_topleft_x, max_ascent_y };
    ret.next_topleft_x = start_topleft_x;

    Vector2 base = next_topleft;
    base.y += fd.ascent_offset;

    for (u32 i = 0; i < count; ++i) {
        i32 codepoint = codepoints[i];

        // Find range
        u32 range_number = 0;
        for (auto &r : fd._packed_ranges) {
            if (r.first_unicode_codepoint_in_range <= codepoint &&
                codepoint < r.first_unicode_codepoint_in_range + r.num_chars) {
                break;
            }
            ++range_number;
        }

        // Not found
        if (range_number == size(fd._packed_ranges)) {
            LOG_F(WARNING, "Codepoint: %i not in packed unicode ranges", codepoint);
            return ret;
        }

        // Get the quad
        stbtt_packedchar *packed_chars = fd._packed_ranges[range_number].chardata_for_range;

        stbtt_aligned_quad glyph_aligned_quad;
        stbtt_GetPackedQuad(packed_chars,
                            fd.atlas_width,
                            fd.atlas_height,
                            codepoint,
                            &base.x,
                            &base.y,
                            &glyph_aligned_quad,
                            0);

        next_topleft.x = base.x;

        // Check if we are exceeding the end_topleft_x
        if (next_topleft.x >= end_topleft_x) {
            return ret;
        }

        // If not, just push the quad and continue
        ret.next_topleft_x = next_topleft.x;
        ++ret.num_chars_pushed;

        // FACTOR THIS OUT!
        AlignedQuad q;
        q.set_from_stbquad(glyph_aligned_quad);
        quad_buffer[i].set_from_aligned_quad(q);
    }

    return ret;
}

std::string str(const FontData &fd) {
    return fmt::format(R"(
        max_ascent_pixels = {}, line_offset_pixels = {}, advance_width = {},
        atlas_width = {}, atlas_height = {}
    )",
                       fd.pixels_per_max_height,
                       fd.bbox_line_advance_pixels,
                       fd.advance_width,
                       fd.atlas_width,
                       fd.atlas_height);
}

} // namespace font

} // namespace eng
