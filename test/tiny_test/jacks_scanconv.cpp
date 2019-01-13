// Scan conversion algorithm by Jack C. Morrison

#include "image.h"

#include <learnogl/rng.h>
#include <scaffold/const_log.h>
#include <tinyobjloader/tiny_obj_loader.h>

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
    i32 i = oy * img.width() + ox;
    for (i32 j = 0; j < size; ++j) {
        for (i32 i1 = i; i1 < i + size; ++i1) {
            img.data()[i1] = color;
        }
        i += img.width();
    }
}

struct Vertex {
    i32 x, y;
    Vector3 rgb;
};

struct Polygon {
    std::vector<Vertex> vertices;
};

Vertex lerp(const Vertex &a, const Vertex &b, float alpha) {
    const float x = lerp(float(a.x), float(b.x), alpha);
    const float y = lerp(float(a.y), float(b.y), alpha);
    const Vector3 rgb = lerp(a.rgb, b.rgb, alpha);
    return Vertex{(i32)roundf(x), (i32)roundf(y), rgb};
}

i32 lerp(i32 a, i32 b, float alpha) { return (i32)roundf(lerp(float(a), float(b), alpha)); }

// Number of subpixels along the y axis
constexpr i32 Y_RES = 8;
// Number of subpixels alogn the x axis
constexpr i32 X_RES = 16;

static_assert(u32(X_RES * Y_RES) < u32(~u8(0)), "Need this for coverage calculation");

struct SubpixelInfo {
    // Subpixel x-wise extents
    std::array<i32, Y_RES> x_left;
    std::array<i32, Y_RES> x_right;

    // Max and min subpixel extent
    i32 xleft_min;
    i32 xleft_max;
    i32 xright_min;
    i32 xright_max;
};

// `y` is the actual resolution row number
void draw_scanline(const Vertex &left, const Vertex &right, const SubpixelInfo &sp, i32 y, Image &img);

void scanconv(const std::vector<Vertex> &verts, Image &img) {
    const Vertex *left;
    const Vertex *next_left;
    const Vertex *right;
    const Vertex *next_right;

    SubpixelInfo sp;

    Vertex vert_left = {};
    Vertex vert_right = {};

    sp.xleft_min = sp.xright_min = std::numeric_limits<i32>::max();
    sp.xleft_max = sp.xright_max = std::numeric_limits<i32>::min();

    // Returns next vertex clockwise
    auto next_vert = [&verts](const Vertex *v) -> const Vertex * {
        const size_t index = v - verts.data();
        return &verts[(index + 1) % verts.size()];
    };

    // Returns previous vertex (therefore anticlockwise)
    auto prev_vert = [&verts](const Vertex *v) -> const Vertex * {
        const size_t index = v - verts.data();
        if (index == 0) {
            return &verts.back();
        }
        return &verts[index - 1];
    };

    left = &verts[0]; // Will intially hold topmost vertex

    for (size_t i = 1; i < verts.size(); ++i) {
        if (left->y < verts[i].y) {
            left = &verts[i];
        }
    }

    next_left = next_vert(left);
    right = left;
    next_right = prev_vert(right);

    for (i32 i = 0; i < Y_RES; ++i) {
        sp.x_left[i] = -999999;
        sp.x_right[i] = -999999;
    }

    for (i32 y = left->y;; --y) {
        log_info("SUBPIXEL SCANLINE: Y = %i", y);

        if (y == next_left->y) {
            left = next_left;
            next_left = next_vert(next_left);
            log_info("NEXT_LEFT");
        }

        if (y == next_right->y) {
            right = next_right;
            next_right = prev_vert(next_right);
            log_info("NEXT_RIGHT");
        }

        if (y < next_left->y || y < next_right->y) {
            draw_scanline(*left, *right, sp, y / Y_RES, img);
            return;
        }

        float alpha_left = float(y - left->y) / (next_left->y - left->y);
        float alpha_right = float(y - right->y) / (next_right->y - right->y);

        // Store interpolated x coordinate
        sp.x_left[y % Y_RES] = lerp(left->x, next_left->x, alpha_left);
        sp.x_right[y % Y_RES] = lerp(right->x, next_right->x, alpha_right);

        // Update extent of orig-res scanline
        sp.xleft_max = std::max(sp.xleft_max, sp.x_left[y % Y_RES]);
        sp.xleft_min = std::min(sp.xleft_min, sp.x_left[y % Y_RES]);

        sp.xright_max = std::max(sp.xright_max, sp.x_right[y % Y_RES]);
        sp.xright_min = std::min(sp.xright_min, sp.x_right[y % Y_RES]);

        // If this is the last sub-scanline of current scanline, interpolate
        // the vertex data. Then draw the scanline
        if (y % Y_RES == (Y_RES - 1)) {
            vert_left = lerp(*left, *next_left, alpha_left);
            vert_right = lerp(*right, *next_right, alpha_right);

            draw_scanline(vert_left, vert_right, sp, y / float(Y_RES), img);

            sp.xleft_min = sp.xright_min = std::numeric_limits<i32>::max();
            sp.xleft_max = sp.xright_max = std::numeric_limits<i32>::min();
        }
    }
}

// Area (in subpixel**2)
u8 compute_pixel_coverage(i32 x_left_sub, const SubpixelInfo &sp) {
    u8 area = 0;
    i32 x_right_sub = x_left_sub + Y_RES - 1;

    for (i32 y = 0; y <= 7; ++y) {
        i32 partial_area = std::min(sp.x_right[y], x_right_sub) - std::max(x_left_sub, sp.x_left[y]) + 1;
        if (partial_area > 0) {
            // Polygon overlaps this pixel
            // 0 < partial_area <= 8, area <= partial_area * 16 <= 128. So yes,
            // fits in a u8.
            assert(i32(area) + partial_area < 255);
            area += u8(partial_area);
        }
    }
    return area;
}

static inline void compute_pixel_mask(i32 x_left_sub, const SubpixelInfo &sp, std::array<u32, Y_RES> &mask) {
    static unsigned leftMaskTable[] = {0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
                                       0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001};
    static unsigned rightMaskTable[] = {0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
                                        0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF};
    unsigned leftMask, rightMask;             /* partial masks */
    i32 x_right_sub = x_left_sub + X_RES - 1; /* right subpixel of pixel */
    i32 y;

    /* shortcut for common case of fully covered pixel */
    if (x_left_sub > sp.xleft_max && x_left_sub < sp.xright_min) {
        for (y = 0; y < Y_RES; y++)
            mask[y] = 0xFFFF;
    } else {
        for (y = 0; y < Y_RES; y++) {
            if (sp.x_left[y] < x_left_sub) /* completely left of pixel*/
                leftMask = 0xFFFF;
            else if (sp.x_left[y] > x_right_sub) /* completely right */
                leftMask = 0;
            else
                leftMask = leftMaskTable[sp.x_left[y] - x_left_sub];

            if (sp.x_right[y] > x_right_sub) /* completely  */
                                             /* right of pixel*/
                rightMask = 0xFFFF;
            else if (sp.x_right[y] < x_left_sub) /*completely left */
                rightMask = 0;
            else
                rightMask = rightMaskTable[sp.x_right[y] - x_left_sub];
            mask[y] = leftMask & rightMask;
        }
    }
}

static inline void draw_pixel(const Vertex &v, u8 fraction_covered, const std::array<u32, Y_RES> &mask,
                              Image &img) {
    (void)mask;

    // Vector3 color = fraction_covered * v.rgb;
    Vector3 color = v.rgb;
    img.pixel(v.x, v.y) = ColorRGBA8{u8(color.x * 255), u8(color.y * 255), u8(color.z * 255), 255};
}

void draw_scanline(const Vertex &vert_left, const Vertex &vert_right, const SubpixelInfo &sp, i32 y_super,
                   Image &img) {
    std::array<u32, Y_RES> mask;

    log_info("DRAW - [%i, %i], [%i, %i]", vert_left.x, vert_left.y, vert_right.x, vert_right.y);

    // x_left_sub stores the left subpixel of the current pixel of the
    // scanline as we scan from left to right
    for (i32 x_left_sub = X_RES * (sp.xleft_min / X_RES); x_left_sub <= sp.xright_max; x_left_sub += X_RES) {
        Vertex v =
            lerp(vert_left, vert_right, float(x_left_sub - sp.xleft_min) / (sp.xright_max - sp.xleft_min));
        u8 area = compute_pixel_coverage(x_left_sub, sp);
        compute_pixel_mask(x_left_sub, sp, mask);

        draw_pixel(v, float(area) / (X_RES * Y_RES), mask, img);
    }
}

int main() {
    // Let's try a triangle
    memory_globals::init();
    {
        Image img(1000, 1000);
        img.clear_with_color(ColorRGBA8::from_hex(0x000000));

        std::vector<Vertex> poly;
        poly.push_back(Vertex{200, 200, Vector3{1.0f, 0.0f, 0.0f}});

        poly.push_back(Vertex{650, 400, Vector3{0.0f, 1.0f, 0.0f}});

        poly.push_back(Vertex{700, 700, Vector3{0.0f, 0.0f, 1.0f}});

        poly.push_back(Vertex{100, 600, Vector3{1.0f, 0.0f, 1.0f}});

        scanconv(poly, img);
        img.write_to_file("jacks_triangle.png");
    }
    memory_globals::shutdown();
}
