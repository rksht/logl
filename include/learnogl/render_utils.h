#include <learnogl/math_ops.h>
#include <learnogl/pmr_compatible_allocs.h>

namespace eng {

// Returns a pointer to 9 floats that are the weights of a separable 9x9 gaussian kernel generated using
// binomial coefficients
pmr::vector<f32> gaussian_kernel(int count);

} // namespace eng
