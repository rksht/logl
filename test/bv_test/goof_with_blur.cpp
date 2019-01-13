#include <learnogl/gl_misc.h>

#include "gaussian_blur_utils.h"

#include <iostream>

int main(int ac, char **av) {
    eng::init_memory();
    DEFERSTAT(eng::shutdown_memory());

    auto kernel_9 = gen_separable_gaussian_kernel(9);

    for (auto n : kernel_9) {
        std::cout << n << ", ";
    }

    puts("\n");

#if 0
    auto other_9 = GenerateSeparableGaussKernel(1.0, 9);

    for (auto n : other_9) {
        std::cout << n << ", ";
    }

    puts("\n");

#endif
}
