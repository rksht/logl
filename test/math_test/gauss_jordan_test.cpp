#ifdef MATH_OPS_TEST

static void test_gauss_jordan() {
    {
        // clang-format off
        double aug_mat[12] = {
            3,  2,  -3, -13,
            4, -3,  6,  7,
            1,  0,  -1, -5
        };
        // clang-format on
        gauss_jordan_elimination(aug_mat);
        print_aug_mat(aug_mat);
    }
    {
        // clang-format off
        double aug_mat[12] = {
            1,  2,  3, -13,
            1,  0,  1,  1,
            3,  4,  7, -9
        };
        // clang-format on
        gauss_jordan_elimination(aug_mat);
        print_aug_mat(aug_mat);
    }
}

static void test_eigen_vecs() {}

int main() { test_gauss_jordan(); }

#endif
