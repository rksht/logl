using namespace fo;

static void print_aug_mat(double *mat) {
    constexpr unsigned n_rows = 3;
    constexpr unsigned n_cols = 4;
    Index2D<unsigned> i{n_cols};
    for (unsigned r = 0; r < n_rows; ++r) {
        printf("%.3f\t%.3f\t%.3f\t| %.3f\n", mat[i(r, 0)], mat[i(r, 1)], mat[i(r, 2)], mat[i(r, 3)]);
    }
}

/*

A Gauss-Jordan elimination routine for linear system with 3 variables and 3
equations

        a0 * x + b0 * y + c0 * z = k0
        a1 * x + b1 * y + c1 * z = k1
        a2 * x + b2 * y + c2 * z = k2

The augmented matrix will look like

        a0      b0      c0      | k0
        a1      b1      c1      | k1
        a2      b2      c2      | k2

*/
template <typename Scalar> static void gauss_jordan_elimination(Scalar *mat) {
    static_assert(std::is_same<Scalar, double>::value || std::is_same<Scalar, float>::value, "");

    constexpr unsigned n_rows = 3;
    constexpr unsigned n_cols = 4;

    const Index2D<unsigned> i{n_cols};

    unsigned r = 0; // current row
    for (unsigned c = 0; c < n_rows; ++c, ++r) {
        // Find row with max element in this column `c`
        unsigned max_row = c;
        Scalar max_row_element = std::abs(mat[i(c, c)]);
        for (unsigned j = c + 1; j < n_rows; ++j) {
            const Scalar elem = std::abs(mat[i(j, c)]);
            if (elem > max_row_element) {
                max_row = j;
                max_row_element = elem;
            }
        }
        // Exchange rows if max element is not in this row
        if (max_row != c) {
            Scalar buf[4];
            memcpy(buf, &mat[i(max_row, 0)], sizeof(buf));
            memcpy(&mat[i(max_row, 0)], &mat[i(r, 0)], sizeof(buf));
            memcpy(&mat[i(r, 0)], buf, sizeof(buf));
        }
        // Multiply full row by 1/max_row_element (not abs'ed)
        const Scalar m = mat[i(r, c)];
        for (unsigned j = 0; j < n_cols; ++j) {
            mat[i(r, j)] = mat[i(r, j)] / m;
        }
        // Clear upper and lower elements of this column to 0
        for (unsigned other_row = 0; other_row < n_rows; ++other_row) {
            if (other_row == r) {
                continue;
            }
            const Scalar first = mat[i(other_row, c)];
            for (unsigned j = c; j < n_cols; ++j) {
                mat[i(other_row, j)] = mat[i(other_row, j)] - first * mat[i(r, j)];
            }
        }
    }
}
