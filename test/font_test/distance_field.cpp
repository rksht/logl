#include "distance_field.h"

struct Point {
    int32_t dx, dy;

    static const Point inside;
    static const Point empty;

    constexpr int32_t dist_sq() const { return dx * dx + dy * dy; }
};

const Point Point::inside = {0, 0};
const Point Point::empty = {9999, 9999};

// Format of a .df file
struct DistanceFieldRaw {
    int32_t w, h;
    float data[];
};

struct Grid {
    std::vector<Point> v;
    int w, h;

    Point get(const std::pair<int, int> &cell) const {
        if (cell.first >= 0 && cell.second >= 0 && cell.first < h && cell.second < w) {
            return v[cell.first * w + cell.second];
        }
        return Point::empty;
    }

    void set(const std::pair<int, int> &cell, Point p) {
        if (cell.first >= 0 && cell.second >= 0 && cell.first < h && cell.second < w) {
            v[cell.first * w + cell.second] = p;
        }
    }

    void _compare(Point &p, int y, int x, int offsetx, int offsety) {
        Point other = get({y + offsety, x + offsetx});
        other.dx += offsetx;
        other.dy += offsety;
        if (other.dist_sq() < p.dist_sq()) {
            p = other;
        }
    }

    void generate_sdf() {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Point p = get({y, x});
                _compare(p, y, x, -1, 0);
                _compare(p, y, x, 0, -1);
                _compare(p, y, x, -1, -1);
                _compare(p, y, x, 1, -1);
                set({y, x}, p);
            }

            for (int x = w - 1; x >= 0; --x) {
                Point p = get({y, x});
                _compare(p, y, x, 1, 0);
                set({y, x}, p);
            }
        }

        for (int y = h - 1; y >= 0; --y) {
            for (int x = w - 1; x >= 0; --x) {
                Point p = get({y, x});
                _compare(p, y, x, 1, 0);
                _compare(p, y, x, 0, 1);
                _compare(p, y, x, -1, 1);
                _compare(p, y, x, 1, 1);
                set({y, x}, p);
            }

            for (int x = 0; x < w; ++x) {
                Point p = get({y, x});
                _compare(p, y, x, -1, 0);
                set({y, x}, p);
            }
        }
    }
};

DistanceField DistanceField::from_binary_image(uint8_t *binary_image, int w, int h) {
    Grid g1{};
    g1.v = std::vector<Point>(w * h);
    g1.w = w;
    g1.h = h;

    Grid g2{};
    g2.v = std::vector<Point>(w * h);
    g2.w = w;
    g2.h = h;

    for (int y = 0; y < h; ++y) {
        const int rowstart = y * w;
        for (int x = 0; x < w; ++x) {
            uint8_t a = binary_image[rowstart + x];
            if (a == 0) {
                g1.set({y, x}, Point::inside);
                g2.set({y, x}, Point::empty);
            } else {
                g1.set({y, x}, Point::empty);
                g2.set({y, x}, Point::inside);
            }
        }
    }

    g1.generate_sdf();
    g2.generate_sdf();

    DistanceField df{w, h, std::vector<float>(w * h)};
    for (int y = 0; y < h; ++y) {
        const int rowstart = y * w;
        for (int x = 0; x < w; ++x) {
            float dist1 = (float)(std::sqrt(double(g1.get({y, x}).dist_sq())));
            float dist2 = (float)(std::sqrt(double(g2.get({y, x}).dist_sq())));
            float dist = dist1 - dist2;
            df.v[rowstart + x] = dist;
        }
    }

    return df;
}

DistanceField DistanceField::from_fgbg_file(const fs::path &file) {
    assert(file.extension().u8string() == ".png");

    int w, h, num_channels;
    uint8_t *buffer = nullptr;

    stbi_info(file.u8string().c_str(), &w, &h, &num_channels);

    DistanceField df;

    if (num_channels == 4) {
        // Copy image into a binary image buffer
        CHECK_EQ_F(4, num_channels, "Need 1 or 4 channels");
        LOG_F(INFO, "Not a binary image, using alpha channel only");

        buffer = stbi_load(file.u8string().c_str(), &w, &h, &num_channels, 4);

        std::vector<uint8_t> alphas;
        alphas.reserve(w * h);

        for (uint8_t *p = buffer, *end = buffer + w * h * 4; p != end; p += 4) {
            alphas.push_back(p[3]);
        }
        df = DistanceField::from_binary_image(alphas.data(), w, h);
    } else if (num_channels == 1) {
        buffer = stbi_load(file.u8string().c_str(), &w, &h, &num_channels, 1);
        df = DistanceField::from_binary_image(buffer, w, h);
    } else {
        LOG_F(ERROR, "Image must have 1 or 4 channels, but it has %d channels", num_channels);
        abort();
    }

    stbi_image_free(buffer);
    return df;
}

DistanceField DistanceField::from_df_file(const fs::path &file) {
    CHECK_F(file.extension().u8string() == ".df", "File name = %s", file.u8string().c_str());
    assert(fs::exists(file));
    DistanceField df;
    FILE *f = fopen(file.u8string().c_str(), "rb");
    fread(&df.w, sizeof(int), 1, f);
    fread(&df.h, sizeof(int), 1, f);

    df.v.resize(df.w * df.h);
    fread(df.v.data(), sizeof(float), df.v.size(), f);
    return df;
}

DistanceField DistanceField::from_fourchannel_image(uint8_t *fourchannel_image, int w, int h,
                                                    int channel_offset) {
    assert(0 <= channel_offset && channel_offset < 4);
    std::vector<uint8_t> alphas;
    alphas.reserve(w * h);

    for (uint8_t *p = fourchannel_image, *end = fourchannel_image + w * h * 4; p != end; p += 4) {
        alphas.push_back(255 - p[channel_offset]);
    }

    return DistanceField::from_binary_image(alphas.data(), w, h);
}

std::vector<uint8_t> DistanceField::clamped_image() const {
    std::vector<uint8_t> img(w * h);

    for (int y = 0; y < h; ++y) {
        const int rowstart = y * w;
        for (int x = 0; x < w; ++x) {
            const int p = rowstart + x;
            const int shifted = v[p] * 3 + 128;
            img[p] = clamp(0, 255, shifted);
        }
    }

    return img;
}

void DistanceField::write_to_file(const fs::path &file) const {
    FILE *f = fopen(file.u8string().c_str(), "wb");
    assert(f != nullptr);

    DistanceFieldRaw raw;
    raw.w = w;
    raw.h = h;

    fwrite(&raw, sizeof(raw), 1, f);
    fwrite(v.data(), sizeof(float), v.size(), f);

    fclose(f);
}
