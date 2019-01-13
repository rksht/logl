#include <learnogl/math_ops.h>
#include <scaffold/debug.h>

using namespace fo;
using namespace math;

static constexpr float kTolerance = 0.0001;

static bool float_equal(const float f1, const float f2) { return std::abs(f1 - f2) <= kTolerance; }

static bool vec_equal(const Vector3 &v1, const Vector3 &v2) {
    return float_equal(v1.x, v2.x) && float_equal(v1.y, v2.y) && float_equal(v1.z, v2.z);
}

static bool mat_equal(const Matrix3x3 &m1, const Matrix3x3 &m2) {
    return vec_equal(m1.x, m2.x) && vec_equal(m1.y, m2.y) && vec_equal(m1.z, m2.z);
}

static bool is_symmetric(const Matrix3x3 m) { return m.y.x == m.x.y && m.z.x == m.x.z && m.z.y == m.y.z; }

bool test(const Matrix3x3 m, const Vector3 &eigenvalues, const Matrix3x3 &eigenvectors) {
    log_assert(is_symmetric(m), "Given matrix is not symmetric");

    SymmetricEigenSolver3x3_Result result;

    eigensolve_sym3x3(m.x.x, m.y.x, m.z.x, m.y.y, m.z.y, m.z.z, result);

    if (!mat_equal(result.eigenvectors, eigenvectors)) {
        printf("NOT CORRECT EIGENVECTORS: \n");
        print_matrix_classic("EXPECTED:", eigenvectors);
        printf("CALCULATED: ", result.eigenvectors);
    }
}

int main() {
}
