// A F32Vec4-like type. There is quite a bit of redundancy in the load/store/conversion/construction
// operations I guess I will just use whichever feels good/easier to read/seems effcient.
#pragma once

#include <xmmintrin.h>

#if __has_include(<pmmintrin.h>)
#include <pmmintrin.h>
#endif

#if __has_include(<smmintrin.h>)
#include <smmintrin.h>
#endif

#if __has_include(<immintrin.h>)
#include <immintrin.h>
#endif

#include <math.h>
#include <stdio.h>

#include <scaffold/debug.h>
#include <scaffold/math_types.h>

namespace simd {

struct Vector4 {
    __m128 m;

    REALLY_INLINE float x() const;
    REALLY_INLINE float y() const;
    REALLY_INLINE float z() const;
    REALLY_INLINE float w() const;

    // -- Set individual components. Returns a new Vector4. Does not modify the given Vector4. So do something
    // like `v=v.set_x(1.0f)`.

    REALLY_INLINE Vector4 set_x(float f) const;
    REALLY_INLINE Vector4 set_y(float f) const;
    REALLY_INLINE Vector4 set_z(float f) const;
    REALLY_INLINE Vector4 set_w(float f) const;

    Vector4() = default;

    // Construction from 4 floats
    REALLY_INLINE Vector4(float x, float y, float z, float w);

    // Constructs from a __m128, implicit.
    Vector4(__m128 m)
        : m(m) {}

    // Constructs from a fo::Vector4. Explicit to denote the potential transfer to an xmm register.
    REALLY_INLINE explicit Vector4(const fo::Vector4 &v)
        : m(_mm_load_ps(reinterpret_cast<const float *>(&v))) {}

    // Constructs from fo::Vector3 and a w component.
    REALLY_INLINE explicit Vector4(const fo::Vector3 &v, float w)
        : m(_mm_set_ps(w, v.z, v.y, v.x)) {}

    // Conversion to __m128 is implicit. Makes sense since this struct is just a wrapper around that.
    operator __m128() const { return m; }

    // Conversion to fo::Vector4 is explicit, denotes a storage into individual float variables (not taking
    // into account compiler optzn)
    REALLY_INLINE explicit operator fo::Vector4() const {
        fo::Vector4 v;
        _mm_store_ps(reinterpret_cast<float *>(&v), m);
        return v;
    }

    // Conversion to fo::Vector3 is also explicit.
    REALLY_INLINE explicit operator fo::Vector3() const { return fo::Vector3(x(), y(), z()); }

    // Alternative to construction from fo::Vector4. Modifies this vector.
    REALLY_INLINE void load(const fo::Vector4 &v) { _mm_load_ps(reinterpret_cast<const float *>(&v)); }

    // Alterntive to conversion to fo::Vector4. Directly stores into given fo::Vector4.
    REALLY_INLINE void store(fo::Vector4 &v) { _mm_store_ps(reinterpret_cast<float *>(&v), m); }
};

struct Matrix4x4 {
    Vector4 _x, _y, _z, _t;

    Matrix4x4() = default;

    // Construction from individual column vectors.
    Matrix4x4(Vector4 x, Vector4 y, Vector4 z, Vector4 t)
        : _x(x)
        , _y(y)
        , _z(z)
        , _t(t) {}

    // Construction from fo::Vector4. Explicit.
    explicit Matrix4x4(const fo::Matrix4x4 &m)
        : _x(m.x)
        , _y(m.y)
        , _z(m.z)
        , _t(m.t) {}

    // Conversion to fo::Vector4
    explicit operator fo::Matrix4x4() const {
        return fo::Matrix4x4{(fo::Vector4)_x, (fo::Vector4)_y, (fo::Vector4)_z, (fo::Vector4)_t};
    }

    Vector4 x() const { return _x; }
    Vector4 y() const { return _y; }
    Vector4 z() const { return _z; }
    Vector4 t() const { return _t; }

    // -- Set individual columns.

    void set_x(Vector4 v) { _x = v; }
    void set_y(Vector4 v) { _y = v; }
    void set_z(Vector4 v) { _z = v; }
    void set_t(Vector4 v) { _t = v; }
};

// Denotes the word 4 word locations in an XMM register.
constexpr int X_ = 0;
constexpr int Y_ = 1;
constexpr int Z_ = 2;
constexpr int W_ = 3;

// Same as MM_SHUFFLE except this one takes the 4 float positions in an xmm register in reverse. Just a bit
// more convenient.
#define MM_SHUFFLE_XYZW(x, y, z, w) ((x) | ((y) << 2) | ((z) << 4) | ((w) << 6))

// Construction

REALLY_INLINE Vector4 from_coords(float x, float y, float z, float w = 0.f) {
    return Vector4{_mm_set_ps(w, z, y, x)};
}

// The array of floats must be aligned to 16 bytes.
REALLY_INLINE Vector4 from_coord_array(float *four_floats) { return Vector4{_mm_load_ps(four_floats)}; }

// Impl of Vector4::Vector4(x, y, z, w)
REALLY_INLINE Vector4::Vector4(float x, float y, float z, float w) { m = _mm_set_ps(w, z, y, x); }

// Component extraction

REALLY_INLINE float get_x(Vector4 v) { return _mm_cvtss_f32(v.m); }

REALLY_INLINE float get_y(Vector4 v) {
    return _mm_cvtss_f32(_mm_shuffle_ps(v.m, v.m, MM_SHUFFLE_XYZW(Y_, Y_, Y_, Y_)));
}

REALLY_INLINE float get_z(Vector4 v) {
    return _mm_cvtss_f32(_mm_shuffle_ps(v.m, v.m, MM_SHUFFLE_XYZW(Z_, Z_, Z_, Z_)));
}

REALLY_INLINE float get_w(Vector4 v) {
    return _mm_cvtss_f32(_mm_shuffle_ps(v.m, v.m, MM_SHUFFLE_XYZW(W_, W_, W_, W_)));
}

REALLY_INLINE float Vector4::x() const { return get_x(*this); }
REALLY_INLINE float Vector4::y() const { return get_y(*this); }
REALLY_INLINE float Vector4::z() const { return get_z(*this); }
REALLY_INLINE float Vector4::w() const { return get_w(*this); }

// Set individual components. Returns a new Vector4, does not modify given vector.

REALLY_INLINE Vector4 Vector4::set_x(float f) const { return from_coords(f, y(), z(), w()); }
REALLY_INLINE Vector4 Vector4::set_y(float f) const { return from_coords(x(), f, z(), w()); }
REALLY_INLINE Vector4 Vector4::set_z(float f) const { return from_coords(x(), y(), f, w()); }
REALLY_INLINE Vector4 Vector4::set_w(float f) const { return from_coords(x(), y(), z(), f); }

// 'Splat' - i.e a vector filled with given float
REALLY_INLINE Vector4 splat(float x) {
#if 0
#if __has_include(<immintrin.h>)
    return Vector4{_mm_broadcast_ss(&x)};
#else
    return Vector4{_mm_set_ps1(x)};
#endif
#endif
    return Vector4{_mm_set_ps1(x)};
}

// Swizzle - implemented as a function template because I was getting compile errors with constexpr.
template <int x_loc, int y_loc, int z_loc, int w_loc> Vector4 swizzle(Vector4 v) {
    return Vector4{_mm_shuffle_ps(v.m, v.m, MM_SHUFFLE_XYZW(x_loc, y_loc, z_loc, w_loc))};
}

REALLY_INLINE Vector4 zero4() { return Vector4(0.0f, 0.0f, 0.0f, 0.0f); }
REALLY_INLINE Vector4 origin() { return Vector4(0.0f, 0.0f, 0.0f, 1.0f); }

REALLY_INLINE Vector4 one4() { return Vector4(1.0f, 1.0f, 1.0f, 1.0f); }
REALLY_INLINE Vector4 one3() { return Vector4(1.0f, 1.0f, 1.0f, 0.0f); }

REALLY_INLINE Vector4 unit_x() { return Vector4(1.0f, 0.0f, 0.0f, 0.0f); }
REALLY_INLINE Vector4 unit_y() { return Vector4(0.0f, 1.0f, 0.0f, 0.0f); }
REALLY_INLINE Vector4 unit_z() { return Vector4(0.0f, 0.0f, 1.0f, 0.0f); }

// Comparisons. Use with care because floating point.

REALLY_INLINE bool operator==(Vector4 a, Vector4 b) {
    return _mm_test_all_ones(_mm_castps_si128(_mm_cmpeq_ps(a.m, b.m)));
}

REALLY_INLINE bool operator<(Vector4 a, Vector4 b) {
    return _mm_test_all_ones(_mm_castps_si128(_mm_cmplt_ps(a.m, b.m)));
}

REALLY_INLINE bool operator<=(Vector4 a, Vector4 b) {
    return _mm_test_all_ones(_mm_castps_si128(_mm_cmple_ps(a.m, b.m)));
}

REALLY_INLINE bool operator>(Vector4 a, Vector4 b) {
    return _mm_test_all_ones(_mm_castps_si128(_mm_cmpgt_ps(a.m, b.m)));
}

REALLY_INLINE bool operator>=(Vector4 a, Vector4 b) {
    return _mm_test_all_ones(_mm_castps_si128(_mm_cmpge_ps(a.m, b.m)));
}

REALLY_INLINE bool operator!=(Vector4 a, Vector4 b) {
    return _mm_test_all_ones(_mm_castps_si128(_mm_cmpneq_ps(a.m, b.m)));
}

// Arithmetic

REALLY_INLINE Vector4 add(Vector4 a, Vector4 b) { return Vector4{_mm_add_ps(a.m, b.m)}; }
REALLY_INLINE Vector4 sub(Vector4 a, Vector4 b) { return Vector4{_mm_sub_ps(a.m, b.m)}; }
REALLY_INLINE Vector4 mul(Vector4 a, Vector4 b) { return Vector4{_mm_mul_ps(a.m, b.m)}; }
REALLY_INLINE Vector4 div(Vector4 a, Vector4 b) { return Vector4{_mm_div_ps(a.m, b.m)}; }

REALLY_INLINE Vector4 mul(float k, Vector4 v) {
    Vector4 kvec = splat(k);
    return mul(v, kvec);
}

REALLY_INLINE Vector4 mul(Vector4 v, float k) {
    Vector4 kvec = splat(k);
    return mul(v, kvec);
}

REALLY_INLINE Vector4 div(Vector4 v, float k) {
    Vector4 kvec = splat(k);
    return div(v, kvec);
}

REALLY_INLINE Vector4 operator^(Vector4 a, Vector4 b) { return _mm_xor_ps(a, b); }

REALLY_INLINE Vector4 negate(Vector4 v) { return sub(zero4(), v); }

// Operator overloads

REALLY_INLINE Vector4 operator+(Vector4 a, Vector4 b) { return add(a, b); }
REALLY_INLINE Vector4 operator-(Vector4 a, Vector4 b) { return sub(a, b); }
REALLY_INLINE Vector4 operator*(Vector4 a, Vector4 b) { return mul(a, b); }
REALLY_INLINE Vector4 operator/(Vector4 a, Vector4 b) { return div(a, b); }

REALLY_INLINE Vector4 operator*(Vector4 a, float k) { return mul(a, k); }
REALLY_INLINE Vector4 operator*(float k, Vector4 a) { return mul(a, k); }
REALLY_INLINE Vector4 operator/(Vector4 a, float k) { return div(a, k); }

// Returns the sum of the components of the vector (i.e the horizontal sum), in the 0th word (x component) of
// the returned __m128. (Not promising what rest of the words are. They are 0 as the current implementation
// stands).
REALLY_INLINE Vector4 hsum(Vector4 v) {
#if __has_include(<pmmintrin.h>)
    return Vector4{_mm_hadd_ps(_mm_hadd_ps(v.m, _mm_setzero_ps()), _mm_setzero_ps())};
#else
    return Vector4{_mm_set_ps(0.f, 0.f, 0.f, get_x(v) + get_y(v) + get_z(v))};
#endif
}

// Returns the dot product, which resides at the 0th (i.e x-th) word of the
// returned __m128. (As before, not promising what the other words are)
REALLY_INLINE Vector4 dot(Vector4 a, Vector4 b) {
#if __has_include(<smmintrin.h>)
    // Reminder - Lower 4 bits of dp_ps tell which words to copy the sum to, high- order 4 bits tell which
    // corresponding words to product in the first place. I am using 1111 for the low order bits so the whole
    // register is filled with the dot product

    // return Vector4{_mm_dp_ps(a.m, b.m, 0b11110001)};
    return Vector4{_mm_dp_ps(a.m, b.m, 0b11111111)};
#else
    return hsum(a * b);
#endif
}

REALLY_INLINE Vector4 sq_mag(Vector4 a) { return dot(a, a); }

REALLY_INLINE float mag(Vector4 a) { return sqrt(get_x(sq_mag(a))); }

// Performs cross product. Treats the vectors as 3D vectors, ignores w component.
REALLY_INLINE Vector4 cross3(Vector4 u, Vector4 v) {
    // Apparently we can save a swizzle this way - result = (u.zxy * v - u * v.zxy).zxy;
    Vector4 u_rot = swizzle<Z_, X_, Y_, W_>(u);
    Vector4 v_rot = swizzle<Z_, X_, Y_, W_>(v);
    Vector4 r = sub(mul(u_rot, v), mul(u, v_rot));
    return swizzle<Z_, X_, Y_, W_>(r);
}

} // namespace simd
