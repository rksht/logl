#include "distance_field.h"
#include "essentials.h"
#include <loguru.hpp>

#ifndef SOURCE_DIR
#error "SOURCE_DIR needs to be defined"
#endif

using namespace fo;
using namespace math;

struct DivideResult {
    Rect2D left, right, occupied;
};

optional<DivideResult> divide_with_rect(const Rect2D &dest, const IVector2 &wh) {
    // Place rect in top left corner
    if (dest.width() < wh.x || dest.height() < wh.y) {
        LOG_F(ERROR, "Failed to put rect of width,height [%f,%f] in rect of dim [%f,%f,%f,%f]", wh.x, wh.y,
              RECT_XYXY(dest));
        return optional<DivideResult>{};
    }

    Rect2D occupied = Rect2D::from_min_and_wh(dest.min, wh);

    // There's two ways to divide the remaining "hexagon" into two rectangles. I will choose the division that
    // has the largest rectangle.

    Rect2D rects[4] = {};

    rects[0].min = occupied.min + IVector2{occupied.width(), 0};
    rects[0].max = dest.max;
    rects[1].min = occupied.min + IVector2{0, occupied.height()};
    rects[1].max = IVector2{occupied.max.x, dest.max.y};

    rects[2].min = occupied.min + IVector2{0, occupied.height()};
    rects[2].max = dest.max;
    rects[3].min = occupied.min + IVector2{occupied.width(), 0};
    rects[3].max = IVector2{dest.max.x, occupied.max.y};

    int largest = 0;
    int32_t largest_area = rects[0].area();
    for (int i = 1; i < 4; ++i) {
        const int32_t area = rects[i].area();
        if (area > largest_area) {
            largest = i;
            largest_area = area;
        }
    }

    Rect2D *left_and_right;
    if (largest < 2) {
        left_and_right = &rects[0];
    } else {
        left_and_right = &rects[2];
    }

    LOG_F(INFO,
          "Divide [%d,%d,%d,%d] with [w,h] = [%d,%d], into LEFT = [%d,%d,%d,%d] and RIGHT = [%d,%d,%d,%d]",
          RECT_XYXY(dest), wh.x, wh.y, RECT_XYXY(left_and_right[0]), RECT_XYXY(left_and_right[1]));

    return DivideResult{left_and_right[0], left_and_right[1], occupied};
}

// The texture coord st will be assigned to the given quads. Overdoing a bit, I know, but this is good
// exercise nonetheless.
void pack_rects(const IVector2 &root_wh, float *root_buffer, std::vector<GlyphInfo> &glyph_infos) {
    // Sorting the rects to pack by decreasing size
    std::sort(glyph_infos.begin(), glyph_infos.end(), [](const auto &info1, const auto &info2) {
        return info1.wh.x * info1.wh.y > info2.wh.x * info2.wh.y;
    });

    std::vector<Rect2D> bins{Rect2D{{0, 0}, root_wh}};

    LOG_SCOPE_F(INFO, "Packing rects");

    for (GlyphInfo &info : glyph_infos) {
        size_t dest_rect_num = bins.size();

        // Choose the smallest rect that can contain the source rect.
        for (int32_t i = (int32_t)bins.size() - 1; i >= 0; --i) {
            const Rect2D &r = bins[i];
            if (r.width() >= info.wh.x && r.height() >= info.wh.y) {
                dest_rect_num = (int32_t)i;
            }
        }

        CHECK_F(dest_rect_num != bins.size(), "Failed to place glyph in some partition");

        // Put the source rectangle and divide the chosen rectangle
        auto divres = divide_with_rect(bins[dest_rect_num], {info.wh.x, info.wh.y}).value();
        bins.erase(bins.begin() + dest_rect_num);
        if (divres.left.area() > 0) {
            bins.push_back(divres.left);
        }
        if (divres.right.area() > 0) {
            bins.push_back(divres.right);
        }
        std::stable_sort(bins.begin(), bins.end(),
                         [](const Rect2D &r1, const Rect2D &r2) { return r1.area() > r2.area(); });

        const Rect2D source_rect = {{0, 0}, info.wh};

        // Blit the source rectangle data
        blit_rect(info.wh, root_wh, source_rect, divres.occupied, divres.occupied.min, info.data,
                  root_buffer);

        Vector2 root_wh_v2 = {(float)root_wh.x, (float)root_wh.y};

        info.tl = Vector2{(float)divres.occupied.min.x, (float)divres.occupied.min.y} / root_wh_v2;
        info.bl = Vector2{(float)divres.occupied.min.x, (float)divres.occupied.max.y} / root_wh_v2;
        info.tr = Vector2{(float)divres.occupied.max.x, (float)divres.occupied.min.y} / root_wh_v2;
        info.br = Vector2{(float)divres.occupied.max.x, (float)divres.occupied.max.y} / root_wh_v2;
        info.tl.y = 1.0f - info.tl.y;
        info.bl.y = 1.0f - info.bl.y;
        info.tr.y = 1.0f - info.tr.y;
        info.br.y = 1.0f - info.br.y;

#define XY(v) (v).x, (v).y

        LOG_F(INFO,
              "Glyph - %c is at rect: [%d,%d,%d,%d], st coords: [(%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), "
              "(%.2f, %.2f)]",
              info.c, RECT_XYXY(divres.occupied), XY(info.tl), XY(info.bl), XY(info.tr), XY(info.br));
    }
}

int main(int ac, char **av) {
    memory_globals::init();
    {
        nfcd_ConfigData *cd =
            simple_parse_file((fs::path(SOURCE_DIR) / "glyph-files.txt").u8string().c_str(), true);
        auto r = nfcd_root(cd);
        fs::path glyph_png_dir = nfcd_to_string(cd, nfcd_object_lookup(cd, r, "glyph_png_dir"));

        fs::path atlas_output_path =
            nfcd_to_string(cd, SIMPLE_MUST(nfcd_object_lookup(cd, r, "atlas_output_path")));

        IVector2 atlas_wh;
        atlas_wh.x = (int32_t)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, r, "atlas_width")));
        atlas_wh.y = (int32_t)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, r, "atlas_height")));

        nfcd_free(cd);

        const fs::path glyph_infos_file = glyph_png_dir / "glyph_infos.data";

        // Read each glyph info (the bounding rect basically)
        std::vector<GlyphInfo> glyph_infos = read_structs_into_vector<GlyphInfo>(glyph_infos_file);

        // Read in each glyph df
        std::vector<std::vector<float>> arr_glyph_df_data;
        arr_glyph_df_data.reserve(glyph_infos.size());
        for (auto &info : glyph_infos) {
            auto df_file = glyph_png_dir / info.file_name;
            DistanceField df = DistanceField::from_df_file(df_file);
            arr_glyph_df_data.push_back(std::move(df.v));
            info.data = arr_glyph_df_data.back().data();
            CHECK_F(info.wh.x == df.w && info.wh.y == df.h);
        }

        // Create distance field atlas
        DistanceField df_atlas(atlas_wh.x, atlas_wh.y);
        pack_rects(atlas_wh, df_atlas.v.data(), glyph_infos);
        df_atlas.write_to_file(atlas_output_path);

        // Now we write the glyph infos back with the texture coords set
        write_file(glyph_infos_file, (const uint8_t *)glyph_infos.data(), vec_bytes(glyph_infos));
    }

    memory_globals::shutdown();
}
