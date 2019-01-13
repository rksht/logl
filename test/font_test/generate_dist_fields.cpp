// Make a distance field of a glyph and save it in the given size
#include "distance_field.h"
#include "essentials.h"
#include <loguru.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <learnogl/stb_image_write.h>

#ifndef SOURCE_DIR
#error "SOURCE_DIR needs to be defined"
#endif

using namespace fo;
using namespace math;

static GLFWwindow *g_window = nullptr;
static bool g_gl_started = false;

static inline void start_gl() {
    if (!g_gl_started) {
        eng::start_gl(&g_window, 200, 200, "generate_dist_fields", 4, 4);
        eng::enable_debug_output(nullptr, nullptr);
    }
}

static inline void close_gl() {
    if (g_gl_started) {
        eng::close_gl();
        glfwTerminate();
    }
}

struct Image {
    std::vector<RGBA8> pixels;
    int w, h;
};

Rect2D get_bounding_rect(RGBA8 *pixels, int w, int h) {
    enum State {
        LEFT_OF_FIRST,
        INSIDE_FIRST,
        MIDDLE,
        INSIDE_SECOND,
        RIGHT_OF_SECOND,
    };

    State s = LEFT_OF_FIRST;
    int32_t i = 0;

    IVector2 first_bar_extent{};
    IVector2 second_bar_extent{};

    constexpr RGBA8 red = {255, 0, 0, 255};

    while (i != w) {
        switch (s) {
        case LEFT_OF_FIRST:
            if (pixels[i] == red) {
                first_bar_extent.x = i;
                s = INSIDE_FIRST;
            }
            break;
        case INSIDE_FIRST:
            if (pixels[i].a == 0) {
                first_bar_extent.y = i;
                s = MIDDLE;
            }
            break;
        case MIDDLE:
            if (pixels[i] == red) {
                second_bar_extent.x = i;
                s = INSIDE_SECOND;
            }
            break;
        case INSIDE_SECOND:
            if (pixels[i].a == 0) {
                second_bar_extent.y = i;
                s = RIGHT_OF_SECOND;
            }
        case RIGHT_OF_SECOND:
            break;
        }

        ++i;
    }

    IVector2 horizontal_extent;

    if (s == MIDDLE || s == INSIDE_FIRST) {
        horizontal_extent.x = 0;
        horizontal_extent.y = first_bar_extent.x;
    } else if (s == INSIDE_SECOND) {
        horizontal_extent.x = first_bar_extent.y;
        horizontal_extent.y = second_bar_extent.x;
    } else if (s == RIGHT_OF_SECOND) {
        horizontal_extent.x = first_bar_extent.y;
        horizontal_extent.y = second_bar_extent.x;
    } else if (s == LEFT_OF_FIRST) {
        horizontal_extent.x = 0;
        horizontal_extent.y = w;
    } else {
        assert("Unpossible");
    }

    // Similarly, get the vertical extents

    first_bar_extent = {};
    second_bar_extent = {};

    i = 0;
    s = LEFT_OF_FIRST;

    while (i != w * h) {
        switch (s) {
        case LEFT_OF_FIRST:
            if (pixels[i] == red) {
                first_bar_extent.x = i / w;
                s = INSIDE_FIRST;
            }
            break;
        case INSIDE_FIRST:
            if (pixels[i].a == 0) {
                first_bar_extent.y = i / w;
                s = MIDDLE;
            }
            break;
        case MIDDLE:
            if (pixels[i] == red) {
                second_bar_extent.x = i / w;
                s = INSIDE_SECOND;
            }
            break;
        case INSIDE_SECOND:
            if (pixels[i].a == 0) {
                second_bar_extent.y = i / w;
                s = RIGHT_OF_SECOND;
            }
            break;
        case RIGHT_OF_SECOND:
            break;
        }

        i += w;
    }

    IVector2 vertical_extent;

    if (s == MIDDLE || s == INSIDE_FIRST) {
        vertical_extent.x = 0;
        vertical_extent.y = first_bar_extent.x;
    } else if (s == INSIDE_SECOND) {
        vertical_extent.x = first_bar_extent.y;
        vertical_extent.y = second_bar_extent.x;
    } else if (s == RIGHT_OF_SECOND) {
        vertical_extent.x = first_bar_extent.y;
        vertical_extent.y = second_bar_extent.x;
    } else if (s == LEFT_OF_FIRST) {
        vertical_extent.x = 0;
        vertical_extent.y = w;
    } else {
        assert("Unpossible");
    }

    return Rect2D{{horizontal_extent.x, vertical_extent.x}, {horizontal_extent.y, vertical_extent.y}};
}

// Minifying the distance field using GL's linear filter. I'm assuming non-power-of-2 textures are supported.
// It's common nowadays.
DistanceField minify_df(const DistanceField &df, int minified_width, int minified_height) {
    // Let's first get over with creating the textures for the source and
    // target framebuffers
    enum TextureNames : GLuint {
        DISTANCE = 0,
        DISTANCE_OUT = 1,
        DEPTH_OUT = 2,
        _COUNT,
    };

    GLuint textures[TextureNames::_COUNT] = {};
    glGenTextures(TextureNames::_COUNT, textures);

    // Generate distance field texture. Since we will minify it we will use
    // LINEAR filter.
    glActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[TextureNames::DISTANCE]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, df.w, df.h, 0, GL_RED, GL_FLOAT, df.v.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Output distance field texture
    glBindTexture(GL_TEXTURE_2D, textures[TextureNames::DISTANCE_OUT]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, minified_width, minified_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Output depth texture
    glBindTexture(GL_TEXTURE_2D, textures[TextureNames::DEPTH_OUT]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, minified_width, minified_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create two framebuffers, one storing the source distance field and one
    // where the resized distance field will be resolved into.
    enum { SOURCE_FB, TARGET_FB };
    GLuint fbs[2];
    glGenFramebuffers(2, fbs);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbs[SOURCE_FB]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbs[TARGET_FB]);

    // Attach the original distance field as a color buffer to the source framebuffer.
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textures[TextureNames::DISTANCE], 0);

    // Attach the textures to render to, to the target framebuffer
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textures[TextureNames::DISTANCE_OUT], 0);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, textures[TextureNames::DEPTH_OUT], 0);

    // Set read/draw buffers for both fbs
    GLuint draw_buffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(ARRAY_SIZE(draw_buffers), draw_buffers);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    // Blit!
    glBlitFramebuffer(0, 0, df.w, df.h, 0, 0, minified_width, minified_height, GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);

    // Read back pixels
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbs[TARGET_FB]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    DistanceField minified_df{minified_width, minified_height};

    glReadPixels(0, 0, minified_width, minified_height, GL_RED, GL_FLOAT, minified_df.v.data());

    return minified_df;
}

int main(int ac, char **av) {
    memory_globals::init();
    {
        start_gl();

        nfcd_ConfigData *cd =
            simple_parse_file((fs::path(SOURCE_DIR) / "glyph-files.txt").u8string().c_str(), true);
        auto r = nfcd_root(cd);
        int minified_width = (int)nfcd_to_number(cd, nfcd_object_lookup(cd, r, "minified_width"));
        int minified_height = (int)nfcd_to_number(cd, nfcd_object_lookup(cd, r, "minified_height"));
        fs::path glyph_png_dir = nfcd_to_string(cd, nfcd_object_lookup(cd, r, "glyph_png_dir"));
        nfcd_free(cd);

        CHECK_F(fs::exists(glyph_png_dir), "");
        CHECK_F(fs::is_directory(glyph_png_dir), "");
        fs::directory_iterator glyph_dir(glyph_png_dir);

        std::vector<fs::path> png_files;

        for (auto &entry : glyph_dir) {
            auto path = entry.path();
            if (path.extension().u8string() == ".png") {
                png_files.push_back(std::move(path));
            }
        }

        // Collect glyph info and create the df textures.

        std::vector<GlyphInfo> glyph_infos;

        for (size_t i = 0; i < png_files.size(); ++i) {
            int w, h, num_channels;
            RGBA8 *image_data = (RGBA8 *)stbi_load(png_files[i].u8string().c_str(), &w, &h, &num_channels, 4);

            CHECK_F(image_data != nullptr, "stbi_load failed to load %s", png_files[i].c_str());
            CHECK_F(num_channels == 4, "Image: %s doesn't have 4 channels", png_files[i].u8string().c_str());

            // Let's first note down the glyph info
            glyph_infos.push_back({});
            auto &info = glyph_infos.back();

            const auto &file = png_files[i];
            std::string filename = file.filename();
            filename = filename.substr(0, filename.find('.'));
            CHECK_F(filename.size() == 1, "File name must be a single character");
            info.c = filename[0];

            info.wh = IVector2{minified_width, minified_height};
            Rect2D glyph_bb = get_bounding_rect(image_data, w, h);

            // Cut the input image with the bounding rect
            Array<RGBA8> cutout_image(memory_globals::default_allocator(), (uint32_t)glyph_bb.area());

            blit_rect({w, h}, {glyph_bb.width(), glyph_bb.height()}, glyph_bb,
                      {{0, 0}, {glyph_bb.width(), glyph_bb.height()}}, {0, 0}, image_data,
                      data(cutout_image));

            stbi_image_free(image_data);

            DistanceField df = DistanceField::from_fourchannel_image((uint8_t *)data(cutout_image),
                                                                     glyph_bb.width(), glyph_bb.height(), 3);

            df = minify_df(df, minified_width, minified_height);

            fs::path df_file = png_files[i];
            change_filename(df_file, "", "df");

            df.write_to_file(df_file);
            LOG_F(INFO, "Generated: %s, Bounding rect = [{%d, %d}, {%d, %d}]", df_file.u8string().c_str(),
                  RECT_XYXY(glyph_bb));
            strncpy(info.file_name, df_file.filename().u8string().c_str(), sizeof(info.file_name));

            // stbi_image_free(image_data);
        }

        // Write the glyph infos to a file
        fs::path glyph_info_file = glyph_png_dir / "glyph_infos.data";
        write_file(glyph_info_file, (uint8_t *)glyph_infos.data(), vec_bytes(glyph_infos));

        close_gl();
    }

    memory_globals::shutdown();
}
