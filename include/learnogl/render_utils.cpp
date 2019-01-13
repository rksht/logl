#include <learnogl/render_utils.h>
#include <numeric>

namespace eng {

struct PascalsTriangle {
    int _row_length;
    pmr::vector<int> _table;

    // Create a pascal triangle of upto (and including) n as the number of items to choose from.
    PascalsTriangle(int n)
        : _row_length(n + 1)
        , _table(make_std_alloc(eng::pmr_default_resource), _row_length * _row_length) {

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

pmr::vector<f32> gaussian_kernel(int count) {
    PascalsTriangle triangle(count - 1);
    pmr::vector<f32> vec(make_std_alloc(eng::pmr_default_resource), count);
    std::copy(triangle.row_begin(n - 1), triangle.row_end(n - 1), vec.begin());
    const f32 sum = std::accumulate(vec.begin(), vec.end());
    for (auto &coeff : vec) {
        coeff /= sum;
    }
    return vec;
}

} // namespace eng
