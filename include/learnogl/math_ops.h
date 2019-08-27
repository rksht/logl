/// Math operations. All matrices are represented in column-major order
#pragma once

#include <learnogl/vmath.h>
#include <scaffold/math_types.h>

#if __has_include(<immintrin.h>)
#    include <immintrin.h>
#endif
#include <xmmintrin.h>

#include <algorithm> // std::swap
#include <assert.h>
#include <cmath>
#include <utility> // std::pair

#include "intel_sse_inverse.h"

#if !defined(RESTRICT)
#    if defined(_MSC_VER)
#        define RESTRICT __restrict
#    else
#        define RESTRICT __restrict__
#    endif
#endif

#ifndef NOT_CONSTEXPR_IN_MSVC
#    if defined(_MSC_VER)
#        define NOT_CONSTEXPR_IN_MSVC
#    elif defined(__CLANG__) || defined(__GNUG__)
#        define NOT_CONSTEXPR_IN_MSVC constexpr
#    endif
#endif

#if !defined(XY)
#    define XY(v) (v).x, (v).y
#endif

#if !defined(XYZ)
#    define XYZ(v) v.x, v.y, v.z
#endif

#if !defined(XYZW)
#    define XYZW(v) v.x, v.y, v.z, v.w
#endif

#if !defined(MAT4_XYZT)
#    define MAT4_XYZT(m) XYZW(m.x), XYZW(m.y), XYZW(m.z), XYZW(m.t)
#endif

// Vector2/3/4 format string for using with printf. `n` is the number of significands to show after decimal.
#define VEC2_FMT(n) "[%." #n "f, %." #n "f]"
#define VEC3_FMT(n) "[%." #n "f, %." #n "f, %." #n "f]"
#define VEC4_FMT(n) "[%." #n "f, %." #n "f, %." #n "f, %." #n "f]"
#define MAT4_FMT(n) "mat4{x=" VEC4_FMT(n) ", y=" VEC4_FMT(n) ", z=" VEC4_FMT(n) ", t=" VEC4_FMT(n) "}"

void invert4x4(const fo::Matrix4x4 *src, fo::Matrix4x4 *dest);

/// Functions and operator-overloads operating on the objects in math_types. This includes the types
/// Matrix4x4, Vector3, Vector4, and Quaternion. To use the operator overloads in the natural infix notation,
/// you must be `using` this namespace(duh). Functions that return a newly created object are not named in any
/// special way, but functions that modify the object provided as argument end with an `_update` suffix, with
/// the first argument being the destination.

namespace eng {
namespace math {

constexpr double pi = 3.14159265358979323846;
constexpr float pi32 = float(pi);

// -- Some oft-used constants
constexpr f64 one_deg_in_rad_f64 = (2.0 * pi) / 360.0;
constexpr f32 one_deg_in_rad = float(one_deg_in_rad_f64);
constexpr fo::Vector3 unit_x = { 1.0, 0.0, 0.0 };
constexpr fo::Vector3 unit_y = { 0.0, 1.0, 0.0 };
constexpr fo::Vector3 unit_z = { 0.0, 0.0, 1.0 };

constexpr fo::Vector4 unit_x_4 = { 1.0, 0.0, 0.0, 0.0 };
constexpr fo::Vector4 unit_y_4 = { 0.0, 1.0, 0.0, 0.0 };
constexpr fo::Vector4 unit_z_4 = { 0.0, 0.0, 1.0, 0.0 };

constexpr fo::Matrix4x4 identity_matrix =
    fo::Matrix4x4{ { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };

constexpr fo::Matrix3x3 identity_matrix3x3 = fo::Matrix3x3{ unit_x, unit_y, unit_z };

constexpr fo::Vector2 zero_2 = { 0.0f, 0.0f };
constexpr fo::Vector3 zero_3 = { 0.0f, 0.0f, 0.0f };
constexpr fo::Vector4 zero_4 = { 0.0f, 0.0f, 0.0f, 0.0f };
constexpr fo::Vector4 zero1_4 = { 0.0f, 0.0f, 0.0f, 1.0f };
constexpr fo::Vector3 one_3 = { 1.0f, 1.0f, 1.0f };
constexpr fo::Matrix4x4 id_4x4 = identity_matrix;
constexpr fo::Matrix3x3 id_3x3 = identity_matrix3x3;

REALLY_INLINE fo::Vector4 point4(const fo::Vector3 &v) { return fo::Vector4(v, 1.0f); }

REALLY_INLINE fo::Vector4 vector4(const fo::Vector3 &v) { return fo::Vector4(v, 0.0f); }

REALLY_INLINE __m128 to_xmm(const fo::Vector4 &v) { return _mm_load_ps(reinterpret_cast<const float *>(&v)); }

#if 0
inline fo::Matrix4x4 mul(const fo::Matrix4x4 &a, const fo::Matrix4x4 &b) {
    fo::Matrix4x4 res{};
    float *p_res = reinterpret_cast<float *>(&res);
    const float *p_a = reinterpret_cast<const float *>(&a);
    const float *p_b = reinterpret_cast<const float *>(&b);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) {
                p_res[i * 4 + j] += p_a[i * 4 + k] * p_b[k * 4 + j];
            }
        }
    }
    return res;
}
#endif

/// Returns the i-th row of the given Matrix4x4
inline constexpr fo::Vector4 mat4_row(const fo::Matrix4x4 &m, uint32_t i) {
    if (i == 0) {
        return fo::Vector4{ m.x.x, m.y.x, m.z.x, m.t.x };
    } else if (i == 1) {
        return fo::Vector4{ m.x.y, m.y.y, m.z.y, m.t.y };
    } else if (i == 2) {
        return fo::Vector4{ m.x.z, m.y.z, m.z.z, m.t.z };
    } else if (i == 3) {
        return fo::Vector4{ m.x.w, m.y.w, m.z.w, m.t.w };
    } else {
        assert(false);
    }
}

inline simd::Vector4 mul(const simd::Matrix4x4 &m, const simd::Vector4 &v) {
    auto r_x = simd::Vector4(_mm_mul_ps(m.x(), simd::splat(v.x())));
    auto r_y = simd::Vector4(_mm_mul_ps(m.y(), simd::splat(v.y())));
    auto r_z = simd::Vector4(_mm_mul_ps(m.z(), simd::splat(v.z())));
    auto r_t = simd::Vector4(_mm_mul_ps(m.t(), simd::splat(v.w())));
    return simd::add(r_x, simd::add(r_y, simd::add(r_z, r_t)));
}

/// Returns the result of the product of matrix `m` and vector `v`
inline fo::Vector4 mul(const fo::Matrix4x4 &m, const fo::Vector4 &v) {
    return (fo::Vector4)mul(
        simd::Matrix4x4(simd::Vector4(m.x), simd::Vector4(m.y), simd::Vector4(m.z), simd::Vector4(m.t)),
        simd::Vector4(v));
}

inline fo::Vector3 transform_point(const fo::Matrix4x4 &m, const fo::Vector3 &v) {
    return fo::Vector3(mul(m, fo::Vector4(v, 1.0f)));
}

inline fo::Vector3 transform_vector(const fo::Matrix4x4 &m, const fo::Vector3 &v) {
    return fo::Vector3(mul(m, fo::Vector4(v, 0.0f)));
}

inline simd::Matrix4x4 mul_mat_mat(const simd::Matrix4x4 &a, const simd::Matrix4x4 &b) {
    return simd::Matrix4x4(mul(a, b.x()), mul(a, b.y()), mul(a, b.z()), mul(a, b.t()));
}

/// Returns the product of the two given matrices, `a` * `b`
inline fo::Matrix4x4 mul_mat_mat(const fo::Matrix4x4 &a, const fo::Matrix4x4 &b) {
    return (fo::Matrix4x4)mul_mat_mat(simd::Matrix4x4(a), simd::Matrix4x4(b));
}

inline fo::Matrix4x4 transpose(const fo::Matrix4x4 &m) {
    fo::Matrix4x4 res = m;
    std::swap(res.y.x, res.x.y);
    std::swap(res.z.x, res.x.z);
    std::swap(res.t.x, res.x.w);
    std::swap(res.z.y, res.y.z);
    std::swap(res.t.y, res.y.w);
    std::swap(res.t.z, res.z.w);
    return res;
}

#if 0
inline fo::Matrix4x4 &transpose_update(fo::Matrix4x4 &m) {
    std::swap(m.y.x, m.x.y);
    std::swap(m.z.x, m.x.z);
    std::swap(m.t.x, m.x.w);
    std::swap(m.z.y, m.y.z);
    std::swap(m.t.y, m.y.w);
    std::swap(m.t.z, m.z.w);
    return m;
}
#endif

inline fo::Matrix4x4 &transpose_update(fo::Matrix4x4 &m) {
    __m128 c_x = _mm_load_ps(reinterpret_cast<const float *>(&m.x)); // x3, x2, x1, x0
    __m128 c_y = _mm_load_ps(reinterpret_cast<const float *>(&m.y)); // y3, y2, y1, y0
    __m128 c_z = _mm_load_ps(reinterpret_cast<const float *>(&m.z)); // z3, z2, z1, z0
    __m128 c_t = _mm_load_ps(reinterpret_cast<const float *>(&m.t)); // w3, w2, w1, w0

    // Want
    // w0, z0, y0, x0
    // w1, z1, y1, x1
    // w2, z2, y2, x2
    // w3, z3, y3, x3

    __m128 t0 = _mm_unpacklo_ps(c_x, c_y); // y1, x1, y0, x0
    __m128 t1 = _mm_unpackhi_ps(c_x, c_y); // y3, x3, y2, x2
    __m128 t2 = _mm_unpacklo_ps(c_z, c_t); // w1, z1, w0, z0
    __m128 t3 = _mm_unpackhi_ps(c_z, c_t); // w3, z3, w2, z2

    simd::Vector4 r_0 =
        _mm_movelh_ps(t0, t2); // 'l'ower two floats of b moved to 'h'igher two floats of `dst`
    simd::Vector4 r_1 =
        _mm_movehl_ps(t2, t0); // 'h'igher two floats of b moved to 'l'ower two floats of `dst`
    simd::Vector4 r_2 = _mm_movelh_ps(t1, t3);
    simd::Vector4 r_3 = _mm_movehl_ps(t3, t1);

    r_0.store(m.x);
    r_1.store(m.y);
    r_2.store(m.z);
    r_3.store(m.t);

    return m;
}

/// Sets `dest` to hold the transpose of matrix `src`.
inline void transpose_update(fo::Matrix4x4 &RESTRICT dest, const fo::Matrix4x4 &RESTRICT src) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            reinterpret_cast<float *>(&dest)[i * 4 + j] = reinterpret_cast<const float *>(&src)[j * 4 + i];
        }
    }
}

inline fo::Matrix4x4 inverse(const fo::Matrix4x4 &m) {
    // Using Intel's inverse implementation, which assumes matrix is in row-major order. Hence, TODO: modify
    // that to use column-major order instead of re-ordering before and after the function call here.
    fo::Matrix4x4 dest;
    invert4x4(&m, &dest);
    transpose_update(dest);
    return dest;
}

// Invert an affine matrix where the 3x3 upper-left sub-matrix is a rotation.
inline fo::Matrix4x4 inverse_rotation_translation(const fo::Matrix4x4 &m) {
    fo::Matrix4x4 inv = { m.x, m.y, m.z, { 0.f, 0.f, 0.f, 1.f } };
    transpose_update(inv);
    auto t = mul(inv, m.t);
    inv.t = { -t.x, -t.y, -t.z, 1.0f };
    return inv;
}

/// Multiples the given scalar to the vector
constexpr inline fo::Vector3 mul(float k, const fo::Vector3 &v) { return { k * v.x, k * v.y, k * v.z }; }

/// Divides the given vector `v` by the scalar `k`
constexpr inline fo::Vector3 div(float k, const fo::Vector3 &v) { return { v.x / k, v.y / k, v.z / k }; }

/// Use operator * for multiplying a matrix to a vector
inline fo::Vector4 operator*(const fo::Matrix4x4 &a, const fo::Vector4 &v) { return mul(a, v); }

/// Use operator * for multiplying a matrix to another matrix
inline fo::Matrix4x4 operator*(const fo::Matrix4x4 &a, const fo::Matrix4x4 &b) { return mul_mat_mat(a, b); }

/// Use operator * for multiplying a scalar to a Vector3
constexpr inline fo::Vector3 operator*(float k, const fo::Vector3 &v) { return mul(k, v); }

constexpr inline fo::Vector3 operator*(const fo::Vector3 &v, float k) { return mul(k, v); }

/// Use operator / for dividing given vector by given scalar
constexpr inline fo::Vector3 operator/(const fo::Vector3 &v, float k) { return div(k, v); }

constexpr inline fo::Vector3 operator/(const fo::Vector3 &a, const fo::Vector3 &b) {
    return fo::Vector3{ a.x / b.x, a.y / b.y, a.z / b.z };
}

constexpr inline fo::Vector3 add(const fo::Vector3 &a, const fo::Vector3 &b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

constexpr inline fo::Vector3 operator+(const fo::Vector3 &a, const fo::Vector3 &b) { return add(a, b); }

constexpr inline fo::Vector4 operator+(const fo::Vector4 &a, const fo::Vector3 &b) {
    return fo::Vector4{ a.x + b.x, a.y + b.y, a.z + b.z, a.w };
}

constexpr inline fo::Vector3 sub(const fo::Vector3 &a, const fo::Vector3 &b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

constexpr inline fo::Vector3 operator-(const fo::Vector3 &a, const fo::Vector3 &b) { return sub(a, b); }

constexpr inline fo::Vector3 negate(const fo::Vector3 &v) { return { -v.x, -v.y, -v.z }; }

constexpr inline fo::Vector3 operator-(const fo::Vector3 &v) { return negate(v); }

constexpr inline fo::Vector4 operator-(const fo::Vector4 &v) { return { -v.x, -v.y, -v.z, -v.w }; }

constexpr inline fo::Vector4 negate_homog(const fo::Vector4 &v) { return { -v.x, -v.y, -v.z, v.w }; }

// Exact equals, usually not useful.
constexpr inline bool operator==(const fo::Vector3 &a, const fo::Vector3 &b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

constexpr inline bool operator!=(const fo::Vector3 &a, const fo::Vector3 &b) { return !(a == b); }

constexpr inline fo::Vector4 operator+(const fo::Vector4 &a, const fo::Vector4 &b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
}

constexpr inline fo::Vector4 operator-(const fo::Vector4 &a, const fo::Vector4 &b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
}

/// Multiplies the scalar to the given Vector4.
inline fo::Vector4 operator*(float k, const fo::Vector4 &v) {
    return fo::Vector4{ k * v.x, k * v.y, k * v.z, k * v.w };
}

inline fo::Vector4 operator*(const fo::Vector4 &v, float k) { return k * v; }

/// Divides given Vector4 by the given scalar.
inline fo::Vector4 operator/(const fo::Vector4 &v, float k) {
    return fo::Vector4{ v.x / k, v.y / k, v.z / k, v.w / k };
}

/* This operation doesn't quite make sense to me as of now.
inline fo::Vector3 operator-(const fo::Vector4 &v) {
    return fo::Vector3{-v.x, -v.y, -v.z, -v.w};
}
*/

inline bool operator==(const fo::Vector4 &a, const fo::Vector4 &b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

inline fo::Vector4 negate_position(const fo::Vector4 &v) {
    assert(v.w == 1);
    return fo::Vector4{ -v.x, -v.y, -v.z, 1 };
}

/// Returns cross product of two Vector3
inline fo::Vector3 cross(const fo::Vector3 &u, const fo::Vector3 &v) {
    // clang-format off
    return fo::Vector3{
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    // clang-format on
}

/// Returns dot product of two Vector3
inline float dot(const fo::Vector3 &a, const fo::Vector3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// Returns dot product of two Vector4
inline float dot(const fo::Vector4 &u, const fo::Vector4 &v) {
    return u.x * v.x + u.y * v.y + u.z * v.z + u.w * v.w;
}

/// Returns the squared magnitude of the given vector
inline float square_magnitude(const fo::Vector3 &v) { return dot(v, v); }

/// Returns the magnitude of the vector
inline float magnitude(const fo::Vector3 &v) { return std::sqrt(square_magnitude(v)); }

/// Returns the corresponding normalized vector
inline fo::Vector3 normalize(const fo::Vector3 &v) {
    const float mag = magnitude(v);
    return fo::Vector3{ v.x / mag, v.y / mag, v.z / mag };
}

inline void normalize_update(fo::Vector3 &v) {
    const float mag = magnitude(v);
    v.x /= mag;
    v.y /= mag;
    v.z /= mag;
}

// -- Don't forget Vector2 :)

constexpr inline fo::Vector2 operator+(const fo::Vector2 &a, const fo::Vector2 &b) {
    return fo::Vector2{ a.x + b.x, a.y + b.y };
}

constexpr inline fo::Vector2 operator-(const fo::Vector2 &a, const fo::Vector2 &b) {
    return fo::Vector2{ a.x - b.x, a.y - b.y };
}

constexpr inline fo::Vector2 operator*(float k, const fo::Vector2 &v) {
    return fo::Vector2{ k * v.x, k * v.y };
}

constexpr inline fo::Vector2 operator*(const fo::Vector2 &v, float k) {
    return fo::Vector2{ k * v.x, k * v.y };
}

constexpr inline fo::Vector2 operator/(const fo::Vector2 &v, float k) {
    return fo::Vector2{ v.x / k, v.y / k };
}

constexpr inline fo::Vector2 operator*(const fo::Vector2 &a, const fo::Vector2 &b) {
    return fo::Vector2{ a.x * b.x, a.y * b.y };
}

constexpr inline fo::Vector2 operator/(const fo::Vector2 &a, const fo::Vector2 &b) {
    return fo::Vector2{ a.x / b.x, a.y / b.y };
}

constexpr inline bool operator==(const fo::Vector2 &a, const fo::Vector2 &b) {
    return a.x == b.x && a.y == b.y;
}

constexpr inline fo::IVector2 operator+(const fo::IVector2 &a, const fo::IVector2 &b) {
    return fo::IVector2{ a.x + b.x, a.y + b.y };
}

constexpr inline fo::IVector2 operator-(const fo::IVector2 &a, const fo::IVector2 &b) {
    return fo::IVector2{ a.x - b.x, a.y - b.y };
}

constexpr inline fo::IVector2 operator*(i32 k, const fo::IVector2 &v) {
    return fo::IVector2{ k * v.x, k * v.y };
}

constexpr inline fo::IVector2 operator*(const fo::IVector2 &v, i32 k) {
    return fo::IVector2{ k * v.x, k * v.y };
}

constexpr inline fo::IVector2 operator/(const fo::IVector2 &v, i32 k) {
    return fo::IVector2{ v.x / k, v.y / k };
}

constexpr inline fo::IVector2 operator*(const fo::IVector2 &a, const fo::IVector2 &b) {
    return fo::IVector2{ a.x * b.x, a.y * b.y };
}

constexpr inline fo::IVector2 operator/(const fo::IVector2 &a, const fo::IVector2 &b) {
    return fo::IVector2{ a.x / b.x, a.y / b.y };
}

constexpr inline bool operator==(const fo::IVector2 &a, const fo::IVector2 &b) {
    return a.x == b.x && a.y == b.y;
}

constexpr inline float square_magnitude(const fo::Vector2 &v) { return v.x * v.x + v.y * v.y; }

inline float magnitude(const fo::Vector2 &v) { return std::sqrt(square_magnitude(v)); }

inline fo::Vector2 &normalize_update(fo::Vector2 &v) {
    float mag = magnitude(v);
    v.x /= mag;
    v.y /= mag;
    return v;
}

constexpr inline fo::Vector3 mul(const fo::Matrix3x3 &m3, const fo::Vector3 &v3) {
    return m3.x * v3.x + m3.y * v3.y + m3.z * v3.z;
}

constexpr inline fo::Vector3 operator*(const fo::Matrix3x3 &m3, const fo::Vector3 &v3) { return mul(m3, v3); }

constexpr inline float cross(const fo::Vector2 &a, const fo::Vector2 &b) { return a.x * b.y - b.y * a.x; }

/// Returns an identity 4x4 matrix
inline fo::Matrix4x4 identity() {
    return fo::Matrix4x4{ { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
}

/// Creates a perspective projection matrix. `near_z` and `far` are the distance of the planes. `vertical_fov`
/// is the field of view angle, in *radians*. `aspect_ratio` is the ratio of viewport width to viewport
/// height. This matrix transforms vertices in eye space to corresponding vertices in clip space.
inline fo::Matrix4x4
perspective_projection(float near_z, float far_z, float vertical_fov, float aspect_ratio) {
    // (t - b) / 2
    const float tb_2 = std::tan(vertical_fov * 0.5f) * near_z;

    // (r - l) / 2
    const float rb_2 = tb_2 * aspect_ratio;

    const float sx = near_z / rb_2;
    const float sy = near_z / tb_2;
    const float sz = -(far_z + near_z) / (far_z - near_z);
    const float qz = -(2.0f * near_z * far_z) / (far_z - near_z);

    // clang-format off
    return fo::Matrix4x4{
        {sx, 0, 0, 0},
        {0, sy, 0, 0},
        {0, 0,  sz, -1.0},
        {0, 0,  qz, 0.0}
    };
    // clang-format on
}

// Same as above, but takes horizontal fov angle instead. `a` is width of viewport divided by height.
inline fo::Matrix4x4 persp_proj(float n, float f, float hfov, float a) {
    const float e = 1.f / std::tan(hfov / 2.f);
    const float sx = e;
    const float sy = e * a;
    const float sz = -(f + n) / (f - n);
    const float qz = -(2.f * n * f) / (f - n);

    // clang-format off
    return fo::Matrix4x4{
        {sx, 0, 0, 0},
        {0, sy, 0, 0},
        {0, 0,  sz, -1.0},
        {0, 0,  qz, 0.0}
    };
    // clang-format on
}

/// Creates an orthographic projection matrix. No near and far planes are to be specified. The z value of the
/// transformed vector remains the same in clip space. Since w = 1 in every clip space vector in orthographic
/// projection, this z remains the same in ndc space too.
inline fo::Matrix4x4 orthographic_projection(float left, float bottom, float right, float top) {
    const float l = left;
    const float r = right;
    const float b = bottom;
    const float t = top;
    // clang-format off
    return fo::Matrix4x4{
        {2.0f/(r - l), 0,            0,      0},
        {0,            2.0f/(t - b), 0,      0},
        {0,            0,            1,      0},  // Transformed vector has same z
        {-(r + l)/(r - l), -(t + b) / (t - b), 0, 1}
    };
    // clang-format on
}

/// Creates an orthographic projection matrix using the given distance to near and far planes, i.e objects
/// will be clipped and the given coordinates for the near plane's left, bottom and right, top corners.
inline fo::Matrix4x4
orthographic_projection(float near_z, float far_z, float left, float bottom, float right, float top) {
    const float n = near_z;
    const float f = far_z;
    const float l = left;
    const float r = right;
    const float b = bottom;
    const float t = top;
    // clang-format off
    return fo::Matrix4x4{
        {2.0f/(r - l),  0,               0,              0},
        {0,             2.0f/(t - b),    0,              0},
        {0,             0,              -2.0f/(f - n),   0},
        {-(r + l)/(r - l), -(t + b)/(t - b), -(f + n)/(f - n), 1}
    };
    // clang-format on
}

struct OrthoRange {
    float min;
    float max;

    OrthoRange() = default;

    OrthoRange(float min, float max)
        : min(min)
        , max(max) {}

    OrthoRange(const fo::Vector2 &v)
        : min(v.x)
        , max(v.y) {}

    operator fo::Vector2() { return fo::Vector2(min, max); }

    OrthoRange reverse() const { return OrthoRange(max, min); }

    OrthoRange negated() const { return OrthoRange(-min, -max); }

    float length() const { return std::abs(min - max); }
};

/// Orthographic projections are the same as applying a non-uniform scale, and then shifting. This function
/// better illustrates that viewpoint. Unlike the others, the clipping range over any component x, y or z is
/// given explicitly. The range [x_source.min, x_source.max] is mapped to the range [x_dest.min, x_dest.max].
/// Ranges can be in increasing or decreasing order, doesn't matter.
fo::Matrix4x4 ortho_proj(const OrthoRange &x_source,
                         const OrthoRange &x_dest,
                         const OrthoRange &y_source,
                         const OrthoRange &y_dest,
                         const OrthoRange &z_source,
                         const OrthoRange &z_dest);

struct OrthoSourceDest {
    OrthoRange source;
    OrthoRange dest;

    OrthoSourceDest() = default;
    OrthoSourceDest(const OrthoRange &source, const OrthoRange &dest)
        : source(source)
        , dest(dest) {}
};

// Another way to do the same
REALLY_INLINE fo::Matrix4x4
ortho_proj(const OrthoSourceDest &x, const OrthoSourceDest &y, const OrthoSourceDest &z) {
    return ortho_proj(x.source, x.dest, y.source, y.dest, z.source, z.dest);
}

// Returns a matrix representing the transform from NDC cube ([-1, 1] in all axes) to a viewport of given
// dimensions and depth.
inline fo::Matrix4x4 ndc_to_viewport_mat4(float viewport_width,
                                          float viewport_height,
                                          float depth_near = 0.0f,
                                          float depth_far = 1.0f) {

    const float depth_scale = 0.5f * (depth_far - depth_near);

    return { { 0.5f, 0.0f, 0.0f, 0.0f },
             { 0.0f, 0.5f, 0.0f, 0.0f },
             { 0.0f, 0.0f, depth_scale, 0.0f },
             { 1.0f, 1.0f, depth_scale + depth_near, 1.0f } };
}

/// Returns a matrix that represents the concatenation of the given by transformation `m`, and a translation
/// by the given (x, y, z) and given matrix. I.e, m' = t * m.
constexpr inline fo::Matrix4x4 translate(const fo::Matrix4x4 &m, const fo::Vector3 &t) {
    // All m.*.w is 0 usually and m.t.w is usually 1
    return fo::Matrix4x4{ { m.x.x, m.x.y, m.x.z, m.x.w },
                          { m.y.x, m.y.y, m.y.z, m.y.w },
                          { m.z.x, m.z.y, m.z.z, m.z.w },
                          { m.t.x + t.x, m.t.y + t.y, m.t.z + t.z, m.t.w } };
}

constexpr inline fo::Matrix4x4 translation_matrix(float x, float y, float z) {
    return fo::Matrix4x4{ fo::Vector4(unit_x, 0.f),
                          fo::Vector4(unit_y, 0.f),
                          fo::Vector4(unit_z, 0.f),
                          fo::Vector4{ x, y, z, 1.0f } };
}

/// Creates a translation matrix with the given translation vector
constexpr inline fo::Matrix4x4 translation_matrix(const fo::Vector3 &t) {
    return translation_matrix(t.x, t.y, t.z);
}

constexpr inline fo::Matrix4x4 xyz_scale_matrix(float x, float y, float z) {
    return fo::Matrix4x4{ fo::Vector4{ x, 0.0f, 0.0f, 0.f },
                          fo::Vector4{ 0.0f, y, 0.0f, 0.f },
                          fo::Vector4{ 0.0f, 0.0f, z, 0.f },
                          fo::Vector4{ 0, 0, 0, 1.0f } };
}

constexpr inline fo::Matrix4x4 xyz_scale_matrix(const fo::Vector3 &t) {
    return xyz_scale_matrix(t.x, t.y, t.z);
}

constexpr inline fo::Matrix4x4 uniform_scale_matrix(float s) { return xyz_scale_matrix(s, s, s); }

/// Similar to translate, but updates the matrix in-place
inline fo::Matrix4x4 &translate_update(fo::Matrix4x4 &m, const fo::Vector3 &t) {
    m.t.x += t.x;
    m.t.y += t.y;
    m.t.z += t.z;
    return m;
}

/// Reflects given vector `v` about the vector `a`. `a` must be a unit-length
/// vector.
inline fo::Vector3 reflect(const fo::Vector3 &v, const fo::Vector3 &a) { return v - 2.0f * dot(v, a) * a; }

/// Returns a matrix that a vector about the given axis `a` by `angle` radians. `a` must be a unit vector.
inline fo::Matrix4x4 rotation_matrix(const fo::Vector3 &a, float angle) {
    float s = std::sin(angle);
    float c = std::cos(angle);
    float oc = 1 - c;

    // clang-format off
    return fo::Matrix4x4{
        {c + oc * a.x * a.x,        oc * a.x * a.y + s * a.z,   oc * a.x * a.z - s * a.y,   0},
        {oc * a.x * a.y - s * a.z,  c + oc * a.y * a.y,         oc * a.y * a.z + s * a.x,   0},
        {oc * a.x * a.z + s * a.y,  oc * a.y * a.z - s * a.x,   c + oc * a.z * a.z,         0},
        {0,                         0,                          0,                          1}
    };
    // clang-format on
}

/// Similar to above but stores the result into `m`
inline void rotation_update(fo::Matrix4x4 &m, const fo::Vector3 &a, float angle) {
    float s = std::sin(angle);
    float c = std::cos(angle);
    float oc = 1 - c;

    m.x.x = c + oc * a.x * a.x;
    m.x.y = oc * a.x * a.y + s * a.z;
    m.x.z = oc * a.x * a.z - s * a.y;
    m.x.w = 0;

    m.y.x = oc * a.x * a.y - s * a.z;
    m.y.y = c + oc * a.y * a.y;
    m.y.z = oc * a.y * a.z + s * a.x;
    m.y.w = 0;

    m.z.x = oc * a.x * a.z + s * a.y;
    m.z.y = oc * a.y * a.z - s * a.x;
    m.z.z = c + oc * a.z * a.z;
    m.z.w = 0;

    m.t.x = 0;
    m.t.y = 0;
    m.t.z = 0;
    m.t.w = 1;
}

/// Handy to have rotation about the 'y' axis explicitly, I guess.
inline fo::Matrix4x4 rotation_about_y(float angle) {
    float s = std::sin(angle);
    float c = std::cos(angle);
    // clang-format off
    return fo::Matrix4x4{
        {c,  0, -s, 0},
        {0,  1, 0, 0},
        {s, 0, c, 0},
        {0,  0, 0, 1}
    };
    // clang-format on
}

inline void rotation_about_y_update(fo::Matrix4x4 &m, float angle) {
    float s = std::sin(angle);
    float c = std::cos(angle);

    m.x.x = c;
    m.x.y = 0;
    m.x.z = -s;
    m.x.w = 0;

    m.y.x = 0;
    m.y.y = 1;
    m.y.z = 0;
    m.y.w = 0;

    m.z.x = s;
    m.z.y = 0;
    m.z.z = c;
    m.z.w = 0;

    m.t.x = 0;
    m.t.y = 0;
    m.t.z = 0;
    m.t.w = 1;
}

/// Returns a rotation in 2D
inline fo::Matrix3x3 rotation_matrix_2d(float angle) {
    return fo::Matrix3x3{ { std::cos(angle), std::sin(angle), 0.0f },
                          { -std::sin(angle), std::cos(angle), 0.0f },
                          { 0.0f, 0.0f, 1.0f } };
}

/// Given a unit vector w, finds a right handed orthonormal set of vectors {u, v, w}.
inline void compute_orthogonal_complements(const fo::Vector3 &w, fo::Vector3 &u, fo::Vector3 &v) {
    // We project given w to either xy, yz, or zx plane. If we project to xy plane, at least one of w's x or y
    // components must be non-zero to avoid getting an INF. Since w is a unit vector, the component with max
    // abs value is guaranteed to be non-zero

    if (std::abs(w.x) > std::abs(w.y)) {
        // w.y is not the max component. One of the other two components must be non-zero. Project onto zx
        // plane. Then rotate by 90 degrees. Doing a ccw rotation, but doesn't matter.
        float inv_length = 1.0f / std::sqrt(w.z * w.z + w.z * w.z);
        u = { w.z * inv_length, 0.f, -w.x * inv_length };
    } else {
        // w.x is not the max component. Project onto yz plane. Rotate by 90 degrees.
        float inv_length = 1.0f / std::sqrt(w.y * w.y + w.z * w.z);
        u = { 0.f, -w.z * inv_length, w.y * inv_length };
    }
    v = normalize(cross(w, u));
}

// Functions operating on quaternions.

/// Creates a quaternion representing a rotation about the given `axis`, which *must* be a unit vector, by
/// the given `angle`.
inline fo::Quaternion versor_from_axis_angle(const fo::Vector3 &axis, float angle) {
    const float s = std::sin(angle / 2);
    const float c = std::cos(angle / 2);
    return fo::Quaternion{ s * axis.x, s * axis.y, s * axis.z, c };
}

/// Calculates the axis and angle of the rotation represented by given versor
inline std::pair<fo::Vector3, float> axis_angle_from_versor(const fo::Quaternion &q) {
    const float angle_by2 = std::acos(q.w);
    const float s = std::sin(angle_by2);
    return std::make_pair(fo::Vector3{ q.x / s, q.y / s, q.z / s }, angle_by2 * 2.0f);
}

/// A versor that applies a 0 degree rotation.
constexpr fo::Quaternion identity_versor = { 0.0, 0.0, 0.0, 1.0 };

/// Returns the inverse of given versor.
constexpr inline fo::Quaternion inverse_versor(const fo::Quaternion &q) {
    return fo::Quaternion{ -q.x, -q.y, -q.z, q.w };
}

/// Exact equals
constexpr inline bool operator==(const fo::Quaternion &p, const fo::Quaternion &q) {
    return p.x == q.x && p.y == q.y && p.z == q.z && p.w == q.w;
}

/// Returns the corresponding normalized quaternion (versor) (Forward declaration)
inline fo::Quaternion normalize(const fo::Quaternion &q) {
    const float mag = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return fo::Quaternion{ q.x / mag, q.y / mag, q.z / mag, q.w / mag };
}

inline void normalize_update(fo::Quaternion &q) {
    const float mag = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    q.x /= mag;
    q.y /= mag;
    q.z /= mag;
    q.w /= mag;
}

/// Returns a * b. The resulting quaternion, after it isnormalized(you have to do it), represents a rotation
/// first by b, then by a.
inline fo::Quaternion mul(const fo::Quaternion &a, const fo::Quaternion &b) {
    // clang-format off
    return fo::Quaternion {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y, // x
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x, // y
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w, // z
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z  // w
    };
    // clang-format on
}

/// Scalar multiplication on a quaternion.
inline fo::Quaternion mul(const fo::Quaternion &q, f32 k) {
    return fo::Quaternion{ q.x * k, q.y * k, q.z * k, q.w * k };
}

inline fo::Quaternion div(const fo::Quaternion &q, f32 k) {
    return fo::Quaternion{ q.x / k, q.y / k, q.z / k, q.w / k };
}

inline f32 square_magnitude(const fo::Quaternion &q) { return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w; }

/// Rotates given vector using the given versor. This returns `q * v * inv(q)` where the vector v is treated
/// as a quaternion with scalar component being 0.
inline fo::Vector3 apply_versor(const fo::Quaternion &q, const fo::Vector3 &v) {
    auto q_vec = fo::Vector3{ q.x, q.y, q.z };
    auto v_0 = mul(q.w * q.w - square_magnitude(q_vec), v);
    auto v_1 = mul(2 * q.w, cross(q_vec, v));
    auto v_2 = mul(2 * dot(q_vec, v), q_vec);
    return add(v_0, add(v_1, v_2));
}

/// Operator * overloaded for quaternion-quaternion multiplication
inline fo::Quaternion operator*(const fo::Quaternion &a, const fo::Quaternion &b) { return mul(a, b); }

/// Returns a rotation matrix corresponding to the given versor
inline fo::Matrix4x4 matrix_from_versor(const fo::Quaternion &q) {
    using fo::Vector4;
    const float x = q.x;
    const float y = q.y;
    const float z = q.z;
    const float w = q.w;
    const float xs = q.x * q.x;
    const float ys = q.y * q.y;
    const float zs = q.z * q.z;
    // clang-format off
    return fo::Matrix4x4 {
        {1 - 2*ys - 2*zs, 2*x*y + 2*w*z,   2*x*z - 2*w*y,   0},
        {2*x*y - 2*w*z,   1 - 2*xs - 2*zs, 2*y*z + 2*w*x,   0},
        {2*x*z + 2*w*y,   2*y*z - 2*w*x,   1 - 2*xs - 2*ys, 0},
        {0,               0,               0,               1}
    };
    // clang-format on
}

// Given a rotation matrix, returns the versor that represents the same rotation. Only the upper-left 3x3 part
// of the matrix is considered, so the matrix doesn't have to be strictly rotational, it can have a
// translation part too. This is a slow function, don't call it frequently.
fo::Quaternion versor_from_matrix(const fo::Matrix4x4 &mat);

/// Performs linear interpolation
template <typename T, typename Real = f32> inline T lerp(const T &a, const T &b, Real alpha) {
    return (Real(1) - alpha) * a + alpha * b;
}

/// Performs linear interpolation, then returns the result normalized
inline fo::Vector3 nlerp(const fo::Vector3 &a, const fo::Vector3 &b, float alpha) {
    fo::Vector3 l = (1 - alpha) * a + alpha * b;
    return l / magnitude(l);
}

/// Performs linear interpolation, then returns the result normalized
inline fo::Vector4 nlerp(const fo::Vector4 &a, const fo::Vector4 &b, float alpha) {
    fo::Vector4 l = (1 - alpha) * a + alpha * b;
    return l / magnitude(l);
}

/// Performs normalized linear interpolation between two versors
inline fo::Quaternion nlerp(const fo::Quaternion &a, const fo::Vector4 &b, float alpha) {
    const float minus = 1.0f - alpha;
    // clang-format off
    fo::Quaternion l{
        minus * a.x + alpha * b.x,
        minus * a.y + alpha * b.y,
        minus * a.z + alpha * b.z,
        minus * a.w + alpha * b.w
    };
    // clang-format on

    float mag = std::sqrt(l.x * l.x + l.y * l.y + l.z * l.z + l.w * l.w);

    return fo::Quaternion{ l.x / mag, l.y / mag, l.z / mag, l.w / mag };
}

/// Performs spherical linear interpolation between the two given versors
#if 0
inline fo::Quaternion slerp(const fo::Quaternion &a, const fo::Quaternion &b, float alpha) {

}
#endif

/// A plane in 3 dimensions represented by its normal N and signed distance D from origin. So the whole
/// quantity can be stored in a single Vector4.
struct Plane3 {
    fo::Vector4 _v;

    Plane3() = default;

    Plane3(const fo::Vector3 &normal, float distance_from_origin) {
        _v.x = normal.x;
        _v.y = normal.y;
        _v.z = normal.z;
        _v.w = distance_from_origin;
    }

    Plane3(const fo::Vector4 &v)
        : _v(v) {}

    // Explicitly convert into Vector4
    explicit operator fo::Vector4() const { return _v; }

    fo::Vector3 normal() const { return fo::Vector3(_v); }

    float distance() const { return _v.w; }
};

/// Transforms given plane (in <N, D> notation) where the plane equation is p(x) = dot(N,x) + D = 0 with the
/// given matrix
inline fo::Vector4 transform_plane(const fo::Matrix4x4 &mat, const fo::Vector4 &plane) {
    fo::Matrix4x4 m = transpose(inverse(mat));
    return m * plane;
}

/// Same as above, but assumes that matrix is orthogonal. Keeping this here, although we could just do this
/// ourselves.
inline fo::Vector4 transform_plane_ortho(const fo::Matrix4x4 &mat, const fo::Vector4 &plane) {
    return mat * plane;
}

/// A ray. Nothing else.
struct Ray {
    fo::Vector3 origin;
    fo::Vector3 direction;

    constexpr Ray(fo::Vector3 origin, fo::Vector3 direction)
        : origin(origin)
        , direction(direction) {}

    static inline Ray from_look_at(fo::Vector3 origin, fo::Vector3 target) {
        return Ray(origin, normalize(target - origin));
    }
};

// -- Eigenvalue and eigenvector related functions --

/// Returned by `eigensolve_sym3x3`
struct SymmetricEigenSolver3x3_Result {
    fo::Vector3 eigenvalues;
    fo::Matrix3x3 eigenvectors;
};

void eigensolve_sym3x3(const fo::Matrix3x3 &symm, SymmetricEigenSolver3x3_Result &out);

/// A full local transform that can be attached to any object that has a scale, position, orientation. The
/// affine matrix this denotes is R * S + T.
struct LocalTransform {
    fo::Vector3 scale;
    fo::Quaternion orientation;
    fo::Vector3 position;

    LocalTransform() = default;

    LocalTransform(const fo::Vector3 &scale, const fo::Quaternion &orientation, const fo::Vector3 &position)
        : scale(scale)
        , orientation(orientation)
        , position(position) {}

    // Returns the matrix form of this transform.
    REALLY_INLINE fo::Matrix4x4 get_mat4() const {
        fo::Matrix4x4 m = matrix_from_versor(orientation) * xyz_scale_matrix(scale);
        return translate_update(m, position);
    }

    // Stores the matrix form of this transform into given `m`.
    REALLY_INLINE void set_mat4(fo::Matrix4x4 &m) const {
        m.x = fo::Vector4{ scale.x, 0.f, 0.f, 0.0f };
        m.y = fo::Vector4{ 0.f, scale.y, 0.f, 0.0f };
        m.z = fo::Vector4{ 0.f, 0.f, scale.z, 0.0f };
        m.t = zero1_4;
        m = matrix_from_versor(orientation) * m;
        translate_update(m, position);
    }

    // Returns a matrix denoting inverse of this transform
    REALLY_INLINE fo::Matrix4x4 get_inv_mat4() const {
        fo::Matrix4x4 m;
        set_inv_mat4(m);
        return m;
    }

    // Stores the inverse transform's matrix into given m.
    REALLY_INLINE void set_inv_mat4(fo::Matrix4x4 &m) const {
        m = translation_matrix(-position);
        m = matrix_from_versor(inverse_versor(orientation)) * m;
        m = xyz_scale_matrix(one_3 / scale) * m;
    }

    static LocalTransform identity() { return LocalTransform{ one_3, identity_versor, fo::Vector3::zero() }; }
};

} // namespace math

} // namespace eng
