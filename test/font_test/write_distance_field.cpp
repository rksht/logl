// Given a distance field file, writes it as an image so it can be seen visually. You have to change the
// dimension of the image in this file. One off program really.
#include "distance_field.h"
#include <cxxopts.hpp>
#include <learnogl/kitchen_sink.h>
#include <scaffold/memory.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <learnogl/stb_image_write.h>

using namespace fo;

int main(int ac, char **av) {
    memory_globals::init();

    cxxopts::Options desc("Allowed options");
    desc.add_options()("p", "path", cxxopts::value<std::string>(), "path to distance field image");

    auto result = desc.parse(ac, av);

    CHECK_F(result.count("p") == 1);

    fs::path path = result["p"].as<std::string>();

    {
        DistanceField df = DistanceField::from_df_file(path);

        auto img = df.clamped_image();

        change_filename(path, "_df_visual", "png");

        LOG_F(INFO, "output file name = %s", path.u8string().c_str());

        int ret = stbi_write_png(path.u8string().c_str(), df.w, df.h, 1, img.data(), 0);
        CHECK_F(ret != 0, "Failed to write df image");
    }
    memory_globals::shutdown();
}
