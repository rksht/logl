// Contains the non-inline functions from math_ops.h

#include "robust_eigensolver.h"

#include <learnogl/kitchen_sink.h>
#include <learnogl/math_ops.h>
#include <learnogl/vmath.h>
#include <scaffold/const_log.h>
#include <scaffold/debug.h>

#if defined(_MSC_VER)
#    include <fvec.h>
#endif

#include <assert.h>
#include <type_traits>

using namespace fo;

static inline void copy_array_to_vec3(const std::array<float, 3> &a, Vector3 &v) {
    v.x = a[0];
    v.y = a[1];
    v.z = a[2];
}

namespace eng {
namespace math {

void eigensolve_sym3x3(const fo::Matrix3x3 &symm, SymmetricEigenSolver3x3_Result &out) {
    std::array<float, 3> evals{};
    std::array<std::array<float, 3>, 3> evecs{};

    gte::SymmetricEigensolver3x3<float> solver;
    solver(symm.x.x, symm.y.x, symm.z.x, symm.y.y, symm.z.y, symm.z.z, true, -1, evals, evecs);

    out.eigenvalues.x = evals[0];
    out.eigenvalues.y = evals[1];
    out.eigenvalues.z = evals[2];

    copy_array_to_vec3(evecs[0], out.eigenvectors.x);
    copy_array_to_vec3(evecs[1], out.eigenvectors.y);
    copy_array_to_vec3(evecs[2], out.eigenvectors.z);
}

Matrix4x4 ortho_proj(const OrthoRange &x_source,
                     const OrthoRange &x_dest,
                     const OrthoRange &y_source,
                     const OrthoRange &y_dest,
                     const OrthoRange &z_source,
                     const OrthoRange &z_dest) {

    // Scale factors
    const float ax = (x_dest.max - x_dest.min) / (x_source.max - x_source.min);
    const float ay = (y_dest.max - y_dest.min) / (y_source.max - y_source.min);
    const float az = (z_dest.max - z_dest.min) / (z_source.max - z_source.min);

    // Shift amounts
    const float tx = x_dest.min - ax * x_source.min;
    const float ty = y_dest.min - ay * y_source.min;
    const float tz = z_dest.min - az * z_source.min;

    // clang-format off
    return fo::Matrix4x4
    {
        {ax, 0.0f, 0.0f, 0.0f},
        {0.0f, ay, 0.0f, 0.0f},
        {0.0f, 0.0f, az, 0.0f},
        {tx, ty, tz, 1.0f}
    };
    // clang-format on
}

fo::Quaternion versor_from_matrix(const fo::Matrix4x4 &mat) {
    struct MatrixAsArray {
        float c[4][4];
        f32 operator()(int col, int row) const { return c[col][row]; }
    };

    auto m = *reinterpret_cast<const MatrixAsArray *>(&mat);

    fo::Quaternion q;
    f32 t;

    if (m(2, 2) <= 0.0f) {
        if (m(0, 0) >= m(1, 1)) {
            t = 1 + m(0, 0) - m(1, 1) - m(2, 2);
            q = fo::Quaternion{ t, m(0, 1) + m(1, 0), m(2, 0) + m(0, 2), m(1, 2) - m(2, 1) };
        } else {
            t = 1 - m(0, 0) + m(1, 1) - m(2, 2);
            q = fo::Quaternion{ m(0, 1) + m(1, 0), t, m(1, 2) + m(2, 1), m(2, 0) - m(0, 2) };
        }
    } else {
        if (m(0, 0) <= -m(1, 1)) {
            t = 1 - m(0, 0) - m(1, 1) + m(2, 2);
            q = fo::Quaternion{ m(2, 0) + m(0, 2), m(1, 2) + m(2, 1), t, m(0, 1) - m(1, 0) };
        } else {
            t = 1 + m(0, 0) + m(1, 1) + m(2, 2);
            q = fo::Quaternion{ m(1, 2) - m(2, 1), m(2, 0) - m(0, 2), m(0, 1) - m(1, 0), t };
        }
    }

    q = div(mul(q, 0.5f), std::sqrt(t));
    return q;
}

} // namespace math

} // namespace eng

#if defined(__clang__) || defined(__GNUG__)
#    define _MM_ALIGN16 alignas(16)
#endif

_MM_ALIGN16 const uint32_t __MASKSIGNs_[4] = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
_MM_ALIGN16 const uint32_t _Sign_PNPN[4] = { 0x00000000, 0x80000000, 0x00000000, 0x80000000 };
_MM_ALIGN16 const uint32_t _Sign_NPNP[4] = { 0x80000000, 0x00000000, 0x80000000, 0x00000000 };
_MM_ALIGN16 const uint32_t _Sign_PNNP[4] = { 0x00000000, 0x80000000, 0x80000000, 0x00000000 };
_MM_ALIGN16 const uint32_t __0FFF_[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 };
_MM_ALIGN16 const float __ZERONE_[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
_MM_ALIGN16 const float __1000_[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

#define _MASKSIGNs_ (*(simd::Vector4 *)&__MASKSIGNs_) // - - - -
#define Sign_PNPN (*(simd::Vector4 *)&_Sign_PNPN)     // + - + -
#define Sign_NPNP (*(simd::Vector4 *)&_Sign_NPNP)     // - + - +
#define Sign_PNNP (*(simd::Vector4 *)&_Sign_PNNP)     // + - - +
#define _0FFF_ (*(simd::Vector4 *)&__0FFF_)           // 0 * * *
#define _ZERONE_ (*(simd::Vector4 *)&__ZERONE_)       // 1 0 0 1
#define _1000_ (*(simd::Vector4 *)&__1000_)           // 1 0 0 0

// Adapting the code from
// https://software.intel.com/en-us/articles/optimized-matrix-library-for-use-with-the-intel-pentiumr-4-processors-sse2-instructions
void invert4x4(const Matrix4x4 *src, Matrix4x4 *dest) {
    using eng::math::to_xmm;

    auto t = eng::math::transpose(*src);
    simd::Vector4 A = _mm_movelh_ps(to_xmm(t.x), to_xmm(t.y)), B = _mm_movehl_ps(to_xmm(t.y), to_xmm(t.x)),
                  C = _mm_movelh_ps(to_xmm(t.z), to_xmm(t.t)), D = _mm_movehl_ps(to_xmm(t.t), to_xmm(t.z));
    simd::Vector4 iA, iB, iC, iD, // partial inverse of the sub-matrices
        DC, AB;
    simd::Vector4 dA, dB, dC, dD; // determinant of the sub-matrices
    simd::Vector4 det, d, d1, d2;
    simd::Vector4 rd;

    //  AB = A# * B
    AB = _mm_mul_ps(_mm_shuffle_ps(A, A, 0x0F), B);
    AB = AB - (simd::Vector4)_mm_mul_ps(_mm_shuffle_ps(A, A, 0xA5), _mm_shuffle_ps(B, B, 0x4E));
    //  DC = D# * C
    DC = _mm_mul_ps(_mm_shuffle_ps(D, D, 0x0F), C);
    DC = DC - (simd::Vector4)_mm_mul_ps(_mm_shuffle_ps(D, D, 0xA5), _mm_shuffle_ps(C, C, 0x4E));

    //  dA = |A|
    dA = _mm_mul_ps(_mm_shuffle_ps(A, A, 0x5F), A);
    dA = _mm_sub_ss(dA, _mm_movehl_ps(dA, dA));
    //  dB = |B|
    dB = _mm_mul_ps(_mm_shuffle_ps(B, B, 0x5F), B);
    dB = _mm_sub_ss(dB, _mm_movehl_ps(dB, dB));

    //  dC = |C|
    dC = _mm_mul_ps(_mm_shuffle_ps(C, C, 0x5F), C);
    dC = _mm_sub_ss(dC, _mm_movehl_ps(dC, dC));
    //  dD = |D|
    dD = _mm_mul_ps(_mm_shuffle_ps(D, D, 0x5F), D);
    dD = _mm_sub_ss(dD, _mm_movehl_ps(dD, dD));

    //  d = trace(AB*DC) = trace(A#*B*D#*C)
    d = _mm_mul_ps(_mm_shuffle_ps(DC, DC, 0xD8), AB);

    //  iD = C*A#*B
    iD = _mm_mul_ps(_mm_shuffle_ps(C, C, 0xA0), _mm_movelh_ps(AB, AB));
    iD = iD + (simd::Vector4)_mm_mul_ps(_mm_shuffle_ps(C, C, 0xF5), _mm_movehl_ps(AB, AB));
    //  iA = B*D#*C
    iA = _mm_mul_ps(_mm_shuffle_ps(B, B, 0xA0), _mm_movelh_ps(DC, DC));
    iA = iA + (simd::Vector4)_mm_mul_ps(_mm_shuffle_ps(B, B, 0xF5), _mm_movehl_ps(DC, DC));

    //  d = trace(AB*DC) = trace(A#*B*D#*C) [continue]
    d = _mm_add_ps(d, _mm_movehl_ps(d, d));
    d = _mm_add_ss(d, _mm_shuffle_ps(d, d, 1));
    d1 = dA * dD;
    d2 = dB * dC;

    //  iD = D*|A| - C*A#*B
    iD = D * _mm_shuffle_ps(dA, dA, 0) - iD;

    //  iA = A*|D| - B*D#*C;
    iA = A * _mm_shuffle_ps(dD, dD, 0) - iA;

    //  det = |A|*|D| + |B|*|C| - trace(A#*B*D#*C)
    det = d1 + d2 - d;
    rd = (__m128)(simd::splat(1.0f) / det);
#ifdef ZERO_SINGULAR
    rd = _mm_and_ps(_mm_cmpneq_ss(det, _mm_setzero_ps()), rd);
#endif

    //  iB = D * (A#B)# = D*B#*A
    iB = _mm_mul_ps(D, _mm_shuffle_ps(AB, AB, 0x33));
    iB = iB - (simd::Vector4)_mm_mul_ps(_mm_shuffle_ps(D, D, 0xB1), _mm_shuffle_ps(AB, AB, 0x66));
    //  iC = A * (D#C)# = A*C#*D
    iC = _mm_mul_ps(A, _mm_shuffle_ps(DC, DC, 0x33));
    iC = iC - (simd::Vector4)_mm_mul_ps(_mm_shuffle_ps(A, A, 0xB1), _mm_shuffle_ps(DC, DC, 0x66));

    rd = _mm_shuffle_ps(rd, rd, 0);
    rd = rd ^ Sign_PNNP;

    //  iB = C*|B| - D*B#*A
    iB = C * _mm_shuffle_ps(dB, dB, 0) - iB;

    //  iC = B*|C| - A*C#*D;
    iC = B * _mm_shuffle_ps(dC, dC, 0) - iC;

    //  iX = iX / det
    iA = iA * rd;
    iB = iB * rd;
    iC = iC * rd;
    iD = iD * rd;

    auto vx = simd::Vector4(_mm_shuffle_ps(iA, iB, 0x77));
    auto vy = simd::Vector4(_mm_shuffle_ps(iA, iB, 0x22));
    auto vz = simd::Vector4(_mm_shuffle_ps(iC, iD, 0x77));
    auto vt = simd::Vector4(_mm_shuffle_ps(iC, iD, 0x22));

    vx.store(dest->x);
    vy.store(dest->y);
    vz.store(dest->z);
    vt.store(dest->t);
}
