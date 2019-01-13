#include <learnogl/kitchen_sink.h>

struct PascalsTriangle {

    int _row_length;
    std::vector<int> _table;

    PascalsTriangle(int n)
        : _row_length(n + 1)
        , _table(_row_length * _row_length) {

        for (int n = 0; n < _row_length; ++n) {
            _table[n * _row_length + 0] = 1;
        }

        for (int k = 1; k < _row_length; ++k) {
            _table[k] = 0;
        }

        for (int n = 1; n < _row_length; ++n) {
            for (int k = 1; k < _row_length; ++k) {
                _table[n * _row_length + k] =
                    _table[(n - 1) * _row_length + k] + _table[(n - 1) * _row_length + (k - 1)];
            }
        }
    }

    auto row_begin(int n) { return _table.begin() + n * _row_length; }
    auto row_end(int n) { return row_begin(n + 1); }
};

// Generates a factor of the separable kernel
inline std::vector<int> gen_binomial_coefficients(int N) {
    assert(N % 2 != 0);
    PascalsTriangle triangle(N - 1);

    std::vector<int> v;
    v.reserve(N);
    std::copy(triangle.row_begin(N - 1), triangle.row_end(N - 1), std::back_inserter(v));
    return v;
}

#if 0
inline std::vector<float> gen_separable_gaussian_kernel(int kernel_size) {
    auto coeffs = gen_binomial_coefficients(kernel_size);

    float weight_sum = float(1u << kernel_size);

    std::vector<float> weights;

    weights.reserve(kernel_size);
    std::transform(coeffs.begin(), coeffs.end(), std::back_inserter(weights), [weight_sum](int coeff) {
        return (float)coeff / weight_sum;
    });

    return weights;
}

inline std::vector<double> GenerateSeparableGaussKernel(double sigma, int kernelSize) {
    if ((kernelSize % 2) != 1) {
        assert(false); // kernel size must be odd number
        return std::vector<double>();
    }

    int halfKernelSize = kernelSize / 2;

    std::vector<double> kernel;
    kernel.resize(kernelSize);

    const double cPI = 3.14159265358979323846;
    double mean = halfKernelSize;
    double sum = 0.0;
    for (int x = 0; x < kernelSize; ++x) {
        kernel[x] = (float)sqrt(exp(-0.5 * (pow((x - mean) / sigma, 2.0) + pow((mean) / sigma, 2.0))) /
                                (2 * cPI * sigma * sigma));
        sum += kernel[x];
    }
    for (int x = 0; x < kernelSize; ++x)
        kernel[x] /= (float)sum;

    return kernel;
}
#endif

