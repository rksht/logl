#include <learnogl/bounding_shapes.h>
#include <scaffold/array.h>
#include <scaffold/memory.h>
#include <learnogl/math_ops.h>
#include <learnogl/kitchen_sink.h>

using namespace fo;
using namespace math;

int main() {
    memory_globals::init();
    {
        Vector3 points[] = {
            {-1.f, -2.f, 1.f},
            {1.f, 0.f, 2.f},
            {2.f, -1.f, 3.f},
            {2.f, -1.f, 2.f}
        };
        uint32_t num_points = sizeof(points) / sizeof(Vector3);

        auto proj = persp_proj(0.1f, 1000.0f, 70 * one_deg_in_rad, 800.0f / 600.0f);

        print_matrix_classic("clip transform", proj);
    }
    memory_globals::shutdown();
}
