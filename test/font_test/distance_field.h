#pragma once

#include <limits>
#include <stdint.h>
#include <vector>

#include <cmath>
#include <learnogl/stb_image.h>

#include <learnogl/kitchen_sink.h>
#include <loguru.hpp>
#include <scaffold/const_log.h>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

struct DistanceField {
    // Using float distances since we will upload to GL as a float texture
    int32_t w, h;
    std::vector<float> v;

    DistanceField() = default;

    DistanceField(int32_t w, int32_t h, std::vector<float> _v)
        : w(w)
        , h(h)
        , v(std::move(_v)) {
        CHECK_F((int32_t)v.size() == w * h, "");
    }

    // Ctor Creates empty distance field
    DistanceField(int32_t w, int32_t h)
        : w(w)
        , h(h)
        , v(w * h) {}

    float *data() { return v.data(); }
    const float *data() const { return v.data(); }

    void free_vector() {
        decltype(v) empty;
        v.swap(empty);
    }

    // Create an image for somewhat visualizing the distance field
    std::vector<uint8_t> clamped_image() const;

    // Writes a `.df` file
    void write_to_file(const fs::path &file) const;

    static DistanceField from_binary_image(uint8_t *binary_image, int w, int h);
    static DistanceField from_fgbg_file(const fs::path &file);

    // Creates a distance field from a 4 channel image. The `channel_offset` denotes which channel should be
    // used for identifying as the pixel as a foreground or a background pixel.
    static DistanceField from_fourchannel_image(uint8_t *fourchannel_image, int w, int h, int channel_offset);
    // Field already created, just reads the data from the file
    static DistanceField from_df_file(const fs::path &file);
};
