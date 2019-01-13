#include <learnogl/math_ops.h>
#include <learnogl/kitchen_sink.h>
#include <scaffold/debug.h>

using namespace fo;
using namespace math;

using namespace math;

static constexpr float kTolerance = 0.00001;

static bool float_equal(const float f1, const float f2) { return std::abs(f1 - f2) <= kTolerance; }

static bool vec_equal(const Vector4 &v1, const Vector4 &v2) {
    return float_equal(v1.x, v2.x) && float_equal(v1.y, v2.y) && float_equal(v1.z, v2.z);
}

static bool mat_equal(const Matrix4x4 &m1, const Matrix4x4 &m2) {
    return vec_equal(m1.x, m2.x) && vec_equal(m1.y, m2.y) && vec_equal(m1.z, m2.z) && vec_equal(m1.t, m2.t);
}

int main() {
    memory_globals::init();
    {

        Matrix4x4 m = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};
        Matrix4x4 s = m;
        transpose_update(m);

        print_matrix_classic("Original", s);
        print_matrix_classic("Transposed", m);

        transpose_update(s);
        log_assert(mat_equal(m, s), "Must be equal");

        Vector3 v = -Vector3{1.0, 1.0, 0.0};
        Vector3 r = reflect(v, unit_y);
        log_info("Reflected = [%f, %f, %f]", XYZ(r));
    }
    memory_globals::shutdown();
}
