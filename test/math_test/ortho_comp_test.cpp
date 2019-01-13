#include <learnogl/math_ops.h>
#include <learnogl/rng.h>

#include <loguru.hpp>

#include <algorithm>
#include <array>
#include <stdio.h>

using namespace fo;
using namespace math;

Vector3 random_vector(float min, float max) {
    return Vector3{(float)rng::random(min, max), (float)rng::random(min, max), (float)rng::random(min, max)};
}

// Creates an arbitrary orthogonal basis, one of the basis vectors being `a`.
// Not necessarily going to be an orthonormal basis.
void make_orthogonal_basis(const Vector3 &a, Vector3 &b, Vector3 &c) {
    CHECK_F(a.x != 0.f || a.y != 0.f || a.z != 0.f && "Must not be a zero-vector");

    std::array<int, 3> k{0, 1, 2}; // Component index

    std::sort(k.begin(), k.end(), [&a](int i, int j) { return std::abs(a[i]) > std::abs(a[j]); });

    if (a[k[1]] == 0.0) {
        CHECK_F(a[k[2]] == 0.0);

        b = Vector3{};
        b[(k[0] + 1) % 3] = a[k[0]];

        c = Vector3{};
        c[(k[0] + 2) % 3] = a[k[0]];

        return;
    }

    // Seeing that if I choose these rather low, the output is also quite low. There's a limit of course. I
    // suppose I just have to adapt according to the ranges at hand.
    const float l2 = std::min(std::abs(1.0f / a[k[0]]), 0.0000000005f);
    const float p0 = std::min(std::abs(1.0f / a[k[0]]), 0.0000000005f);

    b[k[0]] = p0;
    b[k[2]] = l2 * p0;
    // (-a[0] - a[2]) / a[1]
    b[k[1]] = -(a[k[0]] + l2 * a[k[2]]) * p0 / a[k[1]];
    c = cross(a, b);
}

int main() {
    rng::init_rng(static_cast<unsigned>(0xfacade));

    {
        auto f = [](Vector3 a) {
            Vector3 b, c;
            make_orthogonal_basis(a, b, c);
            CHECK_F(!std::isnan(b.x));
            CHECK_F(!std::isnan(b.y));
            CHECK_F(!std::isnan(b.z));
            CHECK_F(!std::isnan(c.x));
            CHECK_F(!std::isnan(c.y));
            CHECK_F(!std::isnan(c.z));

            const float dab = dot(a, b);
            const float dac = dot(a, c);
            const float dbc = dot(b, c);

            printf("[%.6f, %.6f, %.6f]\n[%.6f, %.6f, %.6f]\n[%.6f, %.6f, %.6f]\n", XYZ(a), XYZ(b), XYZ(c));
            printf("a.b = %.6f, a.c = %.6f, b.c = %.6f\n", dab, dac, dbc);
        };

        f(unit_x);
        f(unit_y);
        f(unit_z);
    }

    int largest_dot_i = -1;
    int largest_dot_which = -1;
    float largest_dot = 0.0f;
    Vector3 largest_dot_vecs[3] = {};

    const float range = 10000.0f;

    for (int i = 0; i < 100000; ++i) {
        Vector3 a = random_vector(-range, range);

        Vector3 b;
        Vector3 c;

        if (i == 4676) {
            printf("BREAK");
        }

        make_orthogonal_basis(a, b, c);

        CHECK_F(!std::isnan(b.x));
        CHECK_F(!std::isnan(b.y));
        CHECK_F(!std::isnan(b.z));
        CHECK_F(!std::isnan(c.x));
        CHECK_F(!std::isnan(c.y));
        CHECK_F(!std::isnan(c.z));

        float dots[] = {dot(a, b), dot(b, c), dot(a, c)};

        int max_j = -1;
        for (int j = 0; j < 3; ++j) {
            if (largest_dot < dots[j]) {
                max_j = j;
            }
        }

        if (max_j != -1) {
            largest_dot_which = max_j;
            largest_dot = dots[max_j];
            largest_dot_vecs[0] = a;
            largest_dot_vecs[1] = b;
            largest_dot_vecs[2] = c;
            largest_dot_i = i;
        }

        printf("[%.6f, %.6f, %.6f]\n[%.6f, %.6f, %.6f]\n[%.6f, %.6f, %.6f]\n", XYZ(a), XYZ(b), XYZ(c));
        printf("a.b = %.6f, a.c = %.6f, b.c = %.6f\n", dots[0], dots[1], dots[2]);
        printf("----\n");
    }

    printf("Largest dot = %.7f\n", largest_dot);
    printf("Largest dot vecs - \n [%f, %f, %f], [%f, %f, %f], [%f, %f, %f]\n", XYZ(largest_dot_vecs[0]),
           XYZ(largest_dot_vecs[1]), XYZ(largest_dot_vecs[2]));
    printf("From %s\n", largest_dot_which == 0 ? "a.b" : (largest_dot_which == 1 ? "b.c" : "a.c"));
    printf("Largest dot i = %i\n", largest_dot_i);
}
