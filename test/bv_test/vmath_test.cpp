#include <assert.h>
#include <learnogl/math_types.h>

void assert_equal(simd::Vector4 v, float x, float y, float z, float w = 0.f) {
    alignas(16) float comps[4];
    _mm_store_ps(comps, v.m);
    assert(comps[0] == x);
    assert(comps[1] == y);
    assert(comps[2] == z);
    assert(comps[3] == w);
}

void assert_equal(simd::Vector4 a, simd::Vector4 b) {
    alignas(16) float comps_0[4];
    alignas(16) float comps_1[4];
    _mm_store_ps(comps_0, a.m);
    _mm_store_ps(comps_1, b.m);
    assert(comps_0[0] == comps_1[0]);
    assert(comps_0[1] == comps_1[1]);
    assert(comps_0[2] == comps_1[2]);
    assert(comps_0[3] == comps_1[3]);
}

int main() {
    auto v1 = simd::from_coords(1.0, 2.0, 3.0, 4.0);
    auto v2 = simd::from_coords(1.0, 2.0, 3.0, 4.0);
    auto v3 = v1 + v2;

    alignas(16) float res[4] = {};

    _mm_store_ps(res, v3.m);
    printf("Added: %f %f %f %f\n", res[0], res[1], res[2], res[3]);

    auto v4 = simd::from_coord_array(res);
    auto v5 = simd::swizzle<Y_, Z_, X_, Z_>(v4);

    _mm_store_ps(res, v5.m);
    printf("Swizzled: %f %f %f %f\n", res[0], res[1], res[2], res[3]);

    auto vdot = dot(v1, v2);

    assert_equal(vdot, 30.0, 0.0, 0.0, 0.0);

    // Dot product
    _mm_store_ps(res, dot(v1, v2).m);
    printf("Dotprod: %f %f %f %f\n", res[0], res[1], res[2], res[3]);

    auto s = simd::from_coords(1.0, -1.0, 2.0, 2.0);
    auto r = simd::hsum(s);

    assert_equal(r, 4.0, 0.0, 0.0, 0.0);

    _mm_store_ps(res, r.m);
    printf("HSum: %f %f %f %f\n", res[0], res[1], res[2], res[3]);
}
