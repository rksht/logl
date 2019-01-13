// Make a particle texture

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <learnogl/stb_image_write.h>

#include <cmath>
#include <learnogl/rng.h>
#include <stdint.h>
#include <vector>

struct Color {
    uint8_t r, g, b, a;
};

void make_image(int size, const char *filename) {
    std::vector<Color> img(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int x_off = x - size / 2;
            const int y_off = y - size / 2;

            const float dist_to_center = std::sqrt(x_off * x_off + y_off * y_off);

            float ratio = dist_to_center / (size / 2);
            if (ratio > 1.0) {
                ratio = 0.0;
            } else {
                ratio = 1.0f - ratio;
            }

            uint8_t a = uint8_t(ratio * 255.0);
            const uint8_t r = (uint8_t)rng::random(128, 255);
            const uint8_t g = 0;
            const uint8_t b = (uint8_t)rng::random(0, 255.0);

            img[y * size + x] = Color{r, g, b, a};
        }
    }

    stbi_write_png(filename, size, size, 4, img.data(), 0);
}

int main() {
    rng::init_rng();
    make_image(256, "particle.png");
}
