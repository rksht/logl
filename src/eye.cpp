/// Implements some non-inline functions operating on the eye state.

#include <learnogl/eye.h>
#include <scaffold/debug.h>

using namespace fo;

namespace eng {

namespace eye {

void update_view_transform(const State &e, Matrix4x4 &view_mat) {
    // View matrix (in classic math order, a column of this diagram represents a component of the Matrix4x4)
    //
    //  +-                       ^   -+
    //  | <~~~~~ right ~~~~~>    '    |
    //  |                        '    |
    //  | <~~~~~ up    ~~~~~>   -T    |
    //  |                        '    |
    //  | <~~~~~ -fwd  ~~~~~>    '    |
    //  |                        v    |
    //  | 0       0        0     1    |
    //  +-                           -+
    // The T component is (inv(eye.orientation) * -eye.position)

    // Create the eye orientation's corresponding matrix
    auto eye_mat = math::matrix_from_versor(e.orientation);

    // We have right, up, and fwd aligned to the columns of. Rotation part of view matrix is the inverse of
    // the eye's orientation. Eye's orientation matrix is orthogonal, so the inverse is equal to transpose.
    math::transpose_update(view_mat, eye_mat);

    // view_mat now holds the inverse of eye_mat, with translation part set to [0 0 0 1]. So we can use it to
    // compute the translation part
    view_mat.t = math::negate_position(math::mul(view_mat, Vector4(e.position, 1)));
}

} // namespace eye

using namespace fo;
using namespace math;

void Camera::set_orthogonal_axes(const fo::Vector3 &target_pos,
                                 const fo::Vector3 &current_pos,
                                 const fo::Vector3 &up) {
    _eye.position = current_pos;

    Vector4 z = vector4(normalize(_eye.position - target_pos));
    Vector4 x = vector4(cross(up, z));
    Vector4 y = vector4(up);
    Vector4 p = point4(current_pos);
    // up == y

    _view_xform.x = x;
    _view_xform.y = y;
    _view_xform.z = z;
    _view_xform.t = Vector4{ 0.f, 0.f, 0.f, 1.f };
    transpose_update(_view_xform);
    _view_xform.t = -(_view_xform * p);
    _view_xform.t.w = 1.0f;

    // Update quaternion of camera too.
    _eye.orientation = versor_from_matrix(Matrix4x4{ x, y, z, p });
}

void Camera::look_at(const Vector3 &target_pos, const Vector3 &current_pos, const fo::Vector3 &up_vector) {
    _eye.position = current_pos;
    f32 m = magnitude(up_vector);
    m = std::max(-1.0f, m);
    m = std::min(m, 1.0f);
    f32 angle = std::asin(m);

    auto q = versor_from_axis_angle(up_vector / m, angle);
    _eye.orientation = q * _eye.orientation;
    update_view_transform();
}

} // namespace eng
