#include <learnogl/bounding_shapes.h>
#include <learnogl/gl_misc.h>
#include <learnogl/math_ops.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <learnogl/rng.h>
#include <scaffold/array.h>
#include <scaffold/memory.h>
#include <halpern_pmr/pmr_vector.h>

using namespace fo;
using namespace eng::math;

constexpr auto x = Vector3{ 1, 1, 1 };

constexpr auto y = x[0];

int main() {
    eng::init_memory();
    {
        Vector3 points[] = { { -1.f, -2.f, 1.f }, { 1.f, 0.f, 2.f }, { 2.f, -1.f, 3.f }, { 2.f, -1.f, 2.f } };
        uint32_t num_points = sizeof(points) / sizeof(Vector3);

        auto proj = persp_proj(0.1f, 1000.0f, 70 * one_deg_in_rad, 800.0f / 600.0f);

        print_matrix_classic("clip transform", proj);

        auto inv_proj = inverse(proj);
        print_matrix_classic("inv proj", inv_proj);

        auto scale = xyz_scale_matrix(5, 5, 5);
        auto inv_scale = inverse(scale);
        print_matrix_classic("scale", scale);
        print_matrix_classic("inv_scale", inv_scale);

        auto rot = rotation_about_y(90.0f * one_deg_in_rad);
        auto inv_rot = inverse(rot);
        print_matrix_classic("rot", rot);
        print_matrix_classic("inv_rot", inv_rot);

        auto m = rot * inv_rot;
        print_matrix_classic("mul", m);

        print_matrix_classic("mul proj inv_proj", inv_proj * proj);
        {
            LOG_SCOPE_F(INFO, "ortho range");
#define OMINMAX(r) (r).min, (r).max

            const auto print_and_get =
                [](OrthoRange xs, OrthoRange xd, OrthoRange ys, OrthoRange yd, OrthoRange zs, OrthoRange zd)
                -> Matrix4x4 {
                printf(R"(
                    x - [%f, %f] -> [%f, %f]
                    y - [%f, %f] -> [%f, %f]
                    z - [%f, %f] -> [%f, %f]
                )",
                       OMINMAX(xs),
                       OMINMAX(xd),
                       OMINMAX(ys),
                       OMINMAX(yd),
                       OMINMAX(zs),
                       OMINMAX(zd));

                return ortho_proj(xs, xd, ys, yd, zs, zd);
            };

            const auto mul_and_print = [](const Matrix4x4 &m, const Vector4 &v) {
                const auto r = m * v;
                printf("Projected [%f, %f, %f] -> [%f, %f, %f]\n", XYZ(v), XYZ(r));
            };

            auto m1 = print_and_get({ -100.0f, 100.0f },
                                    { -1.0f, 1.0f },
                                    { 0.0f, 1024.0f },
                                    { 1.0f, -1.0f },
                                    { -100.0f, 100.0f },
                                    { -1.0f, 1.0f })
            // @rksht - Error over there ^. No semicolon.

            mul_and_print(m1, Vector4{ 0.0f, 1024.0f, 0.0f, 1.0f });
            mul_and_print(m1, Vector4{ 0.0f, 0.0f, 0.0f, 1.0f });
            mul_and_print(m1, Vector4{ 0.0f, 512.0f, 0.0f, 1.0f });

#if 0
            auto s = std::string();

            for (int i = 0; i < 8192; ++i) {
                s += (char)rng::random(65, 91);
            }
#endif
        }
        {
            LOG_SCOPE_F(INFO, "Quat from Matrix");
            auto m = rotation_about_y(pi / 4.0f);
            // auto m = rotation_matrix(normalize(Vector3{0.2f, 0.4f, 0.3f}), pi / 4.0f);
            auto q = versor_from_matrix(m);

            string_stream::Buffer ss(memory_globals::default_allocator());
            str_of_matrix(m, ss, true);
            LOG_F(INFO,
                  R"(
            ---- Matrix given ----
%s
                quat = [x = %.3f, y = %.3f, z = %.3f, w = %.3f]
                mag(quat) = %.f
            )",
                  string_stream::c_str(ss),
                  XYZW(q),
                  std::sqrt(square_magnitude(q)));

            auto m1 = matrix_from_versor(q);
            clear(ss);
            str_of_matrix(m1, ss, true);

            auto q1 = versor_from_matrix(m1);

            LOG_F(INFO,
                  R"(
            ---- Back to matrix ----
%s
                quat = [x = %.3f, y = %.3f, z = %.3f, w = %.3f]
                )",
                  string_stream::c_str(ss),
                  XYZW(q));
        }

        {
            LOG_SCOPE_F(INFO, "Six verts quad from vid test");

            for (u32 i = 0; i < 6 * 10; ++i) {
                u32 v_in_quad = i % 6;
                Vector2 v_pos;

                v_pos.x = (2u <= v_in_quad && v_in_quad <= 4) ? 1.0f : -1.0f;
                v_pos.y = (1u <= v_in_quad && v_in_quad <= 3) ? -1.0f : 1.0f;

                LOG_F(INFO, "v_pos of vertex %u = [%.2f, %.2f]", v_in_quad, XY(v_pos));
            }
        }

        {
            auto v = eng::scratch_vector({1, 2, 3, 4});

            for (int i = 0; i < 10000; ++i) {
                v.push_back(i);
            }
        }
    }

    eng::shutdown_memory();
}
