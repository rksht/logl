// Software renderer follwing ssloy's tutorial

#include "image.h"

#include <learnogl/rng.h>
#include <scaffold/const_log.h>
#include <tinyobjloader/tiny_obj_loader.h>

#ifndef MESH_FILE
#error "MESH_FILE not defined"
#endif

constexpr ColorRGBA8 DEFAULT_LINE_COLOR{0, 0, 0, 255};

void draw_axes_on_image(Image &img) {
    const i32 center_y = img.height() / 2;

    for (i32 i = img.width() * center_y, end = img.width() * center_y + img.width(); i != end; ++i) {
        img.data()[i] = ColorRGBA8{255, 0, 255, 255};
    }

    for (i32 i = 0; i < img.height(); ++i) {
        img.data()[i * img.width() + center_y] = ColorRGBA8{255, 0, 255, 255};
    }
}

void draw_dot(i32 x, i32 y, i32 size, Image &img, ColorRGBA8 color) {
    const i32 ox = std::max(x - size / 2, 0);
    const i32 oy = std::max(y - size / 2, 0);
    const i32 corner_x = std::min(ox + size, img.width() - 1);
    const i32 corner_y = std::min(oy + size, img.height() - 1);

    i32 i = oy * img.width() + ox;
    for (i32 j = 0; j < size; ++j) {
        for (i32 i1 = i; i1 < i + size; ++i1) {
            img.data()[i1] = color;
        }
        i += img.width();
    }
}

template <int x_advance>
static inline void octant_0_or_3(i32 i0, i32 j0, i32 delta_x, i32 delta_y, Image &img,
                                 ColorRGBA8 line_color = DEFAULT_LINE_COLOR) {
    Index2D_i32 idx{img.width()};

    i32 j = j0;
    i32 i = i0;
    i32 err = 0;
    i32 times = delta_x;

    while (times--) {
        img.data()[idx(j, i)] = line_color;
        err += delta_y;
        if (2 * err >= delta_x) {
            err -= delta_x;
            ++j;
        }
        i += x_advance;
    }
}

template <int x_advance>
static inline void octant_1_or_2(i32 i0, i32 j0, i32 delta_x, i32 delta_y, Image &img,
                                 ColorRGBA8 line_color = DEFAULT_LINE_COLOR) {
    Index2D_i32 idx{img.width()};

    i32 i = i0;
    i32 j = j0;
    i32 err = 0;

    i32 times = delta_y;

    while (times--) {
        img.data()[idx(j, i)] = line_color;
        err += delta_x;
        if (2 * err >= delta_y) {
            err -= delta_y;
            i += x_advance;
        }
        ++j;
    }
}

void draw_line(i32 x0, i32 y0, i32 x1, i32 y1, Image &img, ColorRGBA8 line_color = DEFAULT_LINE_COLOR) {
    // If the direction vector of the line, as given, is in octants 4 to 7, we
    // can draw it using an algorithm for the octants 0 to 3 by first swapping
    // the endpoints
    if (y0 > y1) {
        i32 temp = y0;
        y0 = y1;
        y1 = temp;
        temp = x0;
        x0 = x1;
        x1 = temp;
    }

    // Call the appropriate function depending on which octant the line vector
    // is in now.
    i32 delta_x = x1 - x0;
    i32 delta_y = y1 - y0;

    if (delta_x > 0) {
        if (delta_x > delta_y) {
            // octant 0
            // debug("OCTANT 0");
            octant_0_or_3<1>(x0, y0, delta_x, delta_y, img, line_color);
        } else {
            // octant 1
            // debug("OCTANT 1");
            octant_1_or_2<1>(x0, y0, delta_x, delta_y, img, line_color);
        }
    } else {
        delta_x = -delta_x;
        if (delta_x < delta_y) {
            // octant 2
            // debug("OCTANT 2");
            octant_1_or_2<-1>(x0, y0, delta_x, delta_y, img, line_color);
        } else {
            // octant 4
            // debug("OCTANT 3");
            octant_0_or_3<-1>(x0, y0, delta_x, delta_y, img, line_color);
        }
    }
}

void line_test(i32 i0, i32 j0, i32 i1, i32 j1, Image &img, ColorRGBA8 line_color) {
    draw_dot(i0, j0, 5, img, line_color);
    draw_dot(i1, j1, 5, img, line_color);

    draw_line(i0, j0, i1, j1, img, line_color);
}

void random_lines_test(Image &img) {
    const i32 center_x = img.width() / 2;
    const i32 center_y = img.height() / 2;

    for (i32 i = 0; i < 50; ++i) {
        i32 i1 = (i32)rng::random(0, img.width());
        i32 j1 = (i32)rng::random(0, img.height());

        ColorRGBA8 line_color = ColorRGBA8::from_hex((u32)rng::random(0, 1 << 24));

        draw_line(center_x, center_y, i1, j1, img, line_color);
    }
}

// Renders the obj in object frame
std::vector<Vector3> load_obj_mesh(const char *objfile) {
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    tinyobj::attrib_t attribs;

    std::string err;
    bool ret = tinyobj::LoadObj(&attribs, &shapes, &materials, &err, objfile);

    log_assert(err.empty(), "");
    log_assert(ret, "");

    log_assert(shapes.size() == 1, "Must have only 1 mesh");

    std::vector<Vector3> vertices;

    // Loop over shapes
    for (size_t s = 0; s < shapes.size(); ++s) {
        // Loop over faces
        size_t index_offset = 0;
        const auto &shape = shapes[s];
        const auto &mesh = shape.mesh;
        for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f) {
            size_t fv = mesh.num_face_vertices[f];
            // Loop over the vertices in the face
            for (size_t v = 0; v < fv; ++v) {
                auto idx = mesh.indices[index_offset + v];
                tinyobj::real_t vx = attribs.vertices[3 * idx.vertex_index + 0];
                tinyobj::real_t vy = attribs.vertices[3 * idx.vertex_index + 1];
                tinyobj::real_t vz = attribs.vertices[3 * idx.vertex_index + 2];
                vertices.push_back(Vector3{vx, vy, vz});
            }
            index_offset += fv;
        }
    }
    return vertices;
}

void render_model(const std::vector<Vector3> &vertices, Image &img) {
    auto draw_side = [&img](Vector3 start, Vector3 end) {
        const float scale = 400;
        start = scale * start;
        end = scale * end;
        i32 sx = (i32)clamp(0.f, float(img.width() - 1), start.x + float(img.width()) / 2);
        i32 sy = clamp(i32(0), img.height() - 1, i32(img.height() / 2.f - start.y));

        i32 ex = (i32)clamp(0.f, float(img.width() - 1), end.x + float(img.width()) / 2);
        i32 ey = (i32)clamp(i32(0), img.height() - 1, i32(img.height() / 2.f - end.y));

        draw_line(sx, sy, ex, ey, img, ColorRGBA8::from_hex(0xf000a0));
    };

    for (size_t i = 0; i < vertices.size() - 3; i += 3) {
        draw_side(vertices[i], vertices[i + 1]);
        draw_side(vertices[i + 1], vertices[i + 2]);
        draw_side(vertices[i + 2], vertices[i]);
    }
}

int main(int argc, char **argv) {
    memory_globals::init();

    rng::init_rng();

    const char *filename = argc == 2 ? argv[1] : "line.png";

    {
        Image img(1000u, 1000u);

        img.clear_with_color(ColorRGBA8{255, 255, 255, 255});

        draw_axes_on_image(img);

        // line_test(90, 100, 20, 40, img, ColorRGBA8::from_hex(0xae3000));
        // line_test(800, 100, 20, 40, img, ColorRGBA8::from_hex(0xae3000));

        // line_test(600, 300, 800, 200, img, ColorRGBA8::from_hex(0xae3000));

        line_test(600, 300, 800, 300, img, ColorRGBA8::from_hex(0x998822));

        auto vertices = load_obj_mesh(MESH_FILE);
        render_model(vertices, img);

        // random_lines_test(img);

        img.write_to_file(filename);
    }
    memory_globals::shutdown();
}
