#include "essentials.h"
#include <argh.h>
#include <scaffold/const_log.h>

#include <fmt/format.h>
#include <iterator>

using namespace fo;

static inline std::vector<Light> gen_point_lights(unsigned count,
                                                  const Array<LocalTransform> &sphere_transforms,
                                                  const Vector3 *vertices,
                                                  const uint16_t *indices,
                                                  size_t num_indices) {

    std::vector<Light> lights;

    constexpr unsigned max_tries = 100;

    std::vector<AABB> bbs;

    std::transform(sphere_transforms.begin(),
                   sphere_transforms.end(),
                   std::back_inserter(bbs),
                   [vertices, indices, num_indices](auto &transform) {
                       return make_aabb(vertices, indices, num_indices, transform.get_mat4());
                   });

    unsigned times_failed = 0;

    for (unsigned i = 0; i < count; ++i) {
        unsigned tries = 0;
        Light light{};

        bool inside_some = false;

        while (tries < max_tries) {
            light.position = random_vector(-scene_radius, scene_radius);
            inside_some = false;
            // Must be outside each bb
            for (auto &bb : bbs) {
                bool inside_this = true;
                if (light.position.x > bb.max.x || light.position.x < bb.min.x)
                    inside_this = false;
                else if (light.position.y > bb.max.y || light.position.y < bb.min.y)
                    inside_this = false;
                else if (light.position.z > bb.max.z || light.position.z < bb.min.z)
                    inside_this = false;

                if (inside_this) {
                    inside_some = true;
                    break;
                }
            }

            ++tries;

            if (!inside_some || tries == max_tries) {
                light.strength = random_vector(0.3, 0.7);

                light.falloff_start = (float)rng::random(0.3, scene_radius);
                light.falloff_end = (float)rng::random(light.falloff_start + 0.5, scene_radius + 1.0);
                light.falloff_end = clamp(light.falloff_end, light.falloff_start, scene_radius);

                lights.push_back(light);

                LOG_F(INFO, "Sphere radius of light %u = %f", i, point_light_sphere(light));

                break;
            }
        }

        if (inside_some) {
            ++times_failed;
        }
    }

    LOG_F(INFO, "Times failed = %u", times_failed);

    return lights;
}

int main(int ac, char **av) {
    memory_globals::init();
    DEFERSTAT(memory_globals::shutdown());

    argh::parser cmdl(av);

    std::string spheresfile;

    if (!(cmdl("spheresfile") >> spheresfile)) {
        CHECK_F(false, "--spheresfile=<path of cubes file> missing");
    }

    unsigned seed;
    cmdl("seed", 0xdeadbeefu) >> seed;

    unsigned num_lights;
    if (!(cmdl("num_lights") >> num_lights)) {
        CHECK_F(false, "--num_lights=<Number of lights> missing");
    }

    auto sphere_transforms = read_sphere_transforms(fs::path(spheresfile));

    rng::init_rng(seed);
    {
        std::vector<LocalTransform> transforms;

        par_shapes_mesh *cube = par_shapes_create_cube();

        shift_par_cube(cube);

        auto lights = gen_point_lights(num_lights,
                                       sphere_transforms,
                                       reinterpret_cast<Vector3 *>(cube->points),
                                       cube->triangles,
                                       cube->ntriangles * 3);

        auto filepath = fmt::format("{}-lights-{:#08x}.dat", num_lights, seed);

        write_file(filepath, reinterpret_cast<uint8_t *>(lights.data()), vec_bytes(lights));

        par_shapes_free_mesh(cube);
    }
}
