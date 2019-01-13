#include <immintrin.h>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <smmintrin.h>

#include <benchmark/benchmark.h>

struct Vector2 {
    float x, y;
};

struct Vector4;

struct Vector3 {
    float x, y, z;

    Vector3() = default;

    /// Constructs a Vector3 from a Vector4 by simply ignoring the w
    /// coordinate. A bit of implicit behavior here, but let' see how it goes.
    constexpr Vector3(const Vector4 &v);

    constexpr Vector3(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z) {}

    /// Array like accessor
    // constexpr float operator[](unsigned i) const { return reinterpret_cast<const float *>(this)[i]; }
    // constexpr float &operator[](unsigned i) { return reinterpret_cast<float *>(this)[i]; }
};

struct alignas(16) Vector4 {
    float x, y, z, w;

    Vector4() = default;

    // Yep. Gotta give the w explicitly.
    constexpr Vector4(const Vector3 &v, float w)
        : x(v.x)
        , y(v.y)
        , z(v.z)
        , w(w) {}

    constexpr Vector4(float x, float y, float z, float w)
        : x(x)
        , y(y)
        , z(z)
        , w(w) {}

    Vector4(__m128 pack) { _mm_store_ps(reinterpret_cast<float *>(this), pack); }

    /// Array like accessor
    // constexpr float operator[](unsigned i) const { return reinterpret_cast<const float *>(this)[i]; }
    // constexpr float &operator[](unsigned i) { return reinterpret_cast<float *>(this)[i]; }
};

constexpr Vector3::Vector3(const Vector4 &v)
    : x(v.x)
    , y(v.y)
    , z(v.z) {}


struct alignas(16) Matrix4x4 {
    Vector4 x, y, z, t;
};

// Version 1, uses linear combination of columns with vector providing the coeffcients
inline Vector4 mul_avx(const Matrix4x4 &a, const Vector4 &v) {
    // Columns of the matrix
    auto c_x = _mm_load_ps(reinterpret_cast<const float *>(&a.x));
    auto c_y = _mm_load_ps(reinterpret_cast<const float *>(&a.y));
    auto c_z = _mm_load_ps(reinterpret_cast<const float *>(&a.z));
    auto c_t = _mm_load_ps(reinterpret_cast<const float *>(&a.t));

    // Coefficients used to construct...
    auto v_x = _mm_broadcast_ss(&v.x);
    auto v_y = _mm_broadcast_ss(&v.y);
    auto v_z = _mm_broadcast_ss(&v.z);
    auto v_w = _mm_broadcast_ss(&v.w);

    // ... a linear combination of these columns
    auto r_x = _mm_mul_ps(c_x, v_x);
    auto r_y = _mm_mul_ps(c_y, v_y);
    auto r_z = _mm_mul_ps(c_z, v_z);
    auto r_t = _mm_mul_ps(c_t, v_w);
    return Vector4(_mm_add_ps(r_x, _mm_add_ps(r_y, _mm_add_ps(r_z, r_t))));
}

// Version 1, uses linear combination of columns with vector providing the coeffcients
inline Vector4 mul_sse(const Matrix4x4 &a, const Vector4 &v) {
    // Columns of the matrix
    auto c_x = _mm_load_ps(reinterpret_cast<const float *>(&a.x));
    auto c_y = _mm_load_ps(reinterpret_cast<const float *>(&a.y));
    auto c_z = _mm_load_ps(reinterpret_cast<const float *>(&a.z));
    auto c_t = _mm_load_ps(reinterpret_cast<const float *>(&a.t));

    auto v_x = _mm_set1_ps(v.x);
    auto v_y = _mm_set1_ps(v.y);
    auto v_z = _mm_set1_ps(v.z);
    auto v_w = _mm_set1_ps(v.w);

    // ... a linear combination of these columns
    auto r_x = _mm_mul_ps(c_x, v_x);
    auto r_y = _mm_mul_ps(c_y, v_y);
    auto r_z = _mm_mul_ps(c_z, v_z);
    auto r_t = _mm_mul_ps(c_t, v_w);
    return Vector4(_mm_add_ps(r_x, _mm_add_ps(r_y, _mm_add_ps(r_z, r_t))));
}


using Matrix4x4_aligned16 = Matrix4x4;


static void BM_mul_avx(benchmark::State& state) {

  for (auto _ : state) {
    Vector4 v{1.0f, 1.0f, 1.0f, 1.0f};

    Matrix4x4 m{
      {1.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 1.0f}
    };
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(m);
    mul_avx(m, v);
  }
}

static void BM_mul_sse(benchmark::State& state) {
  for (auto _ : state) {
    Vector4 v{1.0f, 1.0f, 1.0f, 1.0f};

    Matrix4x4 m{
      {1.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 1.0f}
    };

    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(m);
    mul_sse(m, v);
  }
}
// Register the function as a benchmark
BENCHMARK(BM_mul_avx);
BENCHMARK(BM_mul_sse);

BENCHMARK_MAIN();
