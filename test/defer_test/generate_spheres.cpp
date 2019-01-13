#include "essentials.h"
#include <argh.h>
#include <fmt/format.h>

#include <learnogl/intersection_test.h>
#include <learnogl/random_sampling.h>

#ifndef SOURCE_DIR
#error "Define SOURCE_DIR"
#endif

struct GenAsteroidsResult {
    Array<SceneObject> objects;
    Array<BoundingSphere> bs_list;
};

static GenAsteroidsResult gen_asteroids(unsigned num_asteroids, float very_large_factor) {
    constexpr float usual_max_scale = scene_radius / k_window_height;

    LOG_F(INFO, "Max scale factor = %.3f", usual_max_scale);

    Array<SceneObject> objects(memory_globals::default_allocator());
    resize(objects, num_asteroids);

    // Cache the bounding sphere list
    Array<BoundingSphere> bs_list(memory_globals::default_allocator());
    resize(bs_list, num_asteroids);

    objects[0].kind = ASTEROID;
    objects[0].xform = LocalTransform::identity();
    bs_list[0] = sphere_from_local_transform(objects[0].xform);

    size_t total_times_tried = 1;
    size_t times_gave_up = 0;

    constexpr size_t max_tries = 100;

    for (size_t i = 1; i < num_asteroids; ++i) {
        LOG_IF_F(INFO, (int)((float)i / num_asteroids * 100) % 20 == 0, "Asteroid number = %zu", i);
        size_t times_tried = 0;
        bool found = false;

        float this_max_scale =
            rng::random(0.0, 10.0) < 2.0 ? usual_max_scale * very_large_factor : usual_max_scale;

        objects[i].kind = ASTEROID;

        while (!found && times_tried < max_tries) {
            float scale = (float)rng::random(0.00001, this_max_scale);

            LocalTransform transform;

            transform.scale = fo::Vector3{scale, scale, scale};
            transform.orientation = versor_from_axis_angle(normalize(random_vector(0.5, 0.5)),
                                                           (float)rng::random(0, 90.0) * one_deg_in_rad);
            transform.position = random_point_inside_sphere(scene_radius);

            objects[i].xform = transform;
            bs_list[i] = sphere_from_local_transform(objects[i].xform);

            bool intersects = false;
            size_t j = 0;
            for (; j < i; ++j) {
                intersects = test_sphere_sphere(
                    bs_list[i].center, bs_list[i].radius, bs_list[j].center, bs_list[j].radius);
                if (intersects) {
                    break;
                }
            }

            found = !intersects;

            if (!found) {
                DLOG_F(INFO, "Bar %zu intersected with bar %zu", i, j);
            }

            ++times_tried;
            ++total_times_tried;
        }

        if (!found) {
            ++times_gave_up;
        }
    }

    DLOG_F(INFO,
           "Average tries per cube = %.2f, Intersecting cubes = %zu",
           double(total_times_tried) / num_asteroids,
           times_gave_up);

    return GenAsteroidsResult{std::move(objects), std::move(bs_list)};
}

int main(int ac, char **av) {
    eng::init_non_gl_globals();
    DEFERSTAT(eng::close_non_gl_globals());

    argh::parser cmdl(av);

    unsigned seed;
    cmdl("seed", 0xdeadbeef) >> seed;

    unsigned num_asteroids;

    if (!(cmdl("num-asteroids") >> num_asteroids)) {
        CHECK_F(false, "--num-asteroids=<Number of bars> missing\n");
    }

    float very_large_factor;

    cmdl("large", 10.0f) >> very_large_factor;

    float max_wall_extent;
    cmdl("max-wall-extent", scene_radius / 10.0f) >> max_wall_extent;

    auto result = gen_asteroids(num_asteroids, very_large_factor);

    auto filepath = fmt::format("{}-asteroids-{:#08x}.dat", num_asteroids, seed);

    write_file(
        filepath, reinterpret_cast<uint8_t *>(data(result.objects)), vec_bytes(result.objects));
}
