#include "image.h"

#include <scaffold/const_log.h>
#include <vector>

struct Vertex {
    i32 x, y;
};

static inline Vertex *next_vertex(std::vector<Vertex> &list, Vertex *v) {
    size_t i = v - list.data();
    return &list[(i + 1) % list.size()];
}

static inline Vertex *prev_vertex(std::vector<Vertex> &list, Vertex *v) {
    size_t i = v - list.data();
    if (i == 0) {
        return &list[list.size() - 1];
    }
    return list[i - 1];
}

static void draw_horizontal_line_list(HLineList *, i32);
static void scan_edge(i32, i32, i32, i32, i32, i32, struct HLine **);

i32 fill_convex_polygon(std::vector<Vertex> &vertex_list, ColorRGBA8 color, i32 x_offset, i32 y_offset) {
    i32 i, min_index_l, max_index, min_index_r, skip_first, temp;
    i32 min_point_y, max_point_y, top_is_flat, left_edge_dir;
    i32 next_index, cur_index, prev_index;
    i32 delta_xn, delta_yn, delta_xp, delta_yp;

    HLineList working_line_list;
    HLine *edge_point_ptr;
    Vertex *vert_ptr;

    auto index_forward = [&verts](i32 i) { return (i + 1) % verts.size(); };

    auto index_backward = [&verts](i32 i) {
        if (i == 0) {
            return verts.size() - 1;
        }
        return i - 1;
    };

    // begin

    vert_ptr = vertex_list.data();
    assert(vertex_list.size() != 0);

    max_point_y = min_point_y = vert_ptr[min_index_l = max_index = 0].y;

    for (i = 0; i < vertex_list.size(); ++i) {
        if (vert_ptr[i].y < min_point_y) {
            min_point_y = vert_ptr[min_index_l = i].y;
        } else if (vert_ptr[i].y > max_point_y) {
            max_point_y = vert_ptr[max_index = i].y;
        }
    }

    if (min_point_y == max_point_y) {
        log_info("0 HEIGHT POLYGON");
        return 1;
    }

    min_index_r = min_index_l;

    // Scan in ccw order to find last top-edge point
    while (vert_ptr[min_index_r].y == min_point_y) {
        min_index_r = index_forward(min_index_r);
    }
    min_index_r = index_backward(min_index_r);

    // Scan in cw order to find first top-edge point
    while (vert_ptr[min_index_l].y == min_point_y) {
        min_index_l = index_backward(min_index_l);
    }
    min_index_l = index_forward(min_index_l);
}
