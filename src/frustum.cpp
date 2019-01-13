// Culling against the viewing frustum

#include <learnogl/math_ops.h>

struct FrustumPlanes {
    Vector4 front; // Near
    Vector4 back;  // Far
    Vector4 right;
    Vector4 left;
    Vector4 top;
    Vector4 bottom;

    void init_from_projection_matrix(const fo::Matrix4x4 &proj_mat);
};


// Implementation

using namespace fo;

FrustumPlanes::init_from_projection_matrix(const Matrix4x4 &proj_mat) {
    const auto row_0 = mat4_row(proj_mat, 0);
    const auto row_1 = mat4_row(proj_mat, 1);
    const auto row_2 = mat4_row(proj_mat, 2);
    const auto row_3 = mat4_row(proj_mat, 3);

    front = row_3 + row_2;
    back = row_3 - row_2;
    left = row_3 + row_0;
    right = row_3 - row_0;
    bottom = row_3 + row_1;
    top = row_3 - row_1;
}
