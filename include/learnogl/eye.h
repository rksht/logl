#pragma once

#include <learnogl/math_ops.h>

namespace eng {

/// Provides an eye and some functions that control its orientation and position, and set up a corresponding
/// view transform. The associated view transform must be updated after calling ny function that modifies the
/// eye state. These functions take a non-const reference to the eye state.
namespace eye {

/// Stores the current eye basis' position and orientation wrt world basis
struct State {
    fo::Quaternion orientation; // Current orientation wrt the world basis
    fo::Vector3 position;       // Current position of eye
};

/// Creates an eye basis where the 'forward' vector of the eye in world basis becomes the negative z axis
inline State toward_negz(f32 zpos = 0.2f) {
    return State{ math::identity_versor, fo::Vector3{ 0, 0, zpos } };
}

/// A bird's eye view
inline State toward_negy(f32 zpos = 0.0f) {
    return State{ math::versor_from_axis_angle(math::unit_x, 270.0f * math::one_deg_in_rad),
                  fo::Vector3{ 0, 0, zpos } };
}

/// Updates the given view matrix to be in accordance with the eye's state.
void update_view_transform(const State &e, fo::Matrix4x4 &view_mat);

/// Rotates the eye using the given versor
inline void rotate(State &e, const fo::Quaternion &q) {
    auto new_orientation = math::mul(q, e.orientation);
    math::normalize_update(new_orientation);
    e.orientation = new_orientation;
}

/// Returns the forward direction of the eye (direction the eye is looking at)
inline fo::Vector3 forward(const State &e) {
    return math::negate(math::apply_versor(e.orientation, math::unit_z));
}

/// Returns the upward direction of the eye (direction the eye is looking at)
inline fo::Vector3 up(const State &e) { return math::apply_versor(e.orientation, math::unit_y); }

/// Returns the right hand direction of the eye
inline fo::Vector3 right(const State &e) { return math::apply_versor(e.orientation, math::unit_x); }

/// Rotate the eye about its current local z-axis(the `forward` direction) by the given angle
inline void roll(State &e, f32 radians) {
    auto axis = math::apply_versor(e.orientation, math::unit_z);
    rotate(e, math::versor_from_axis_angle(axis, radians));
}

/// Rotate the eye about its current local y-axis (the 'up' direction) by the given angle
inline void yaw(State &e, f32 radians) {
    auto axis = math::apply_versor(e.orientation, math::unit_y);
    rotate(e, math::versor_from_axis_angle(axis, radians));
}

/// Rotate the eye about its current local x-axis (the 'right' direction) by the given angle
inline void pitch(State &e, f32 radians) {
    auto axis = math::apply_versor(e.orientation, math::unit_x);
    rotate(e, math::versor_from_axis_angle(axis, radians));
}

} // namespace eye

struct Camera {
    eye::State _eye;
    fo::Matrix4x4 _view_xform; // Needs to be updated everytime camera moves or rotates
    fo::Matrix4x4 _proj_xform; // Needs to be updated if viewport dimensions or new/far plane changes

    // These properties don't usually change once set
    f32 _near_z;
    f32 _far_z;
    f32 _aspect_h_on_w;
    f32 _y_fov;
    f32 _near_plane_y_extent;
    f32 _far_plane_y_extent;

    bool _needs_update = false;

    // ## Getters

    fo::Vector3 position() const { return _eye.position; }
    fo::Quaternion orientation() const { return _eye.orientation; }

    fo::Matrix4x4 view_xform() const { return _view_xform; }
    fo::Matrix4x4 proj_xform() const { return _proj_xform; }
    fo::Matrix4x4 view_proj_xform() const { return math::mul_mat_mat(view_xform(), proj_xform()); }

    fo::Vector3 forward() const { return eye::forward(_eye); }
    fo::Vector3 up() const { return eye::up(_eye); }
    fo::Vector3 right() const { return eye::right(_eye); }

    bool needs_update() const { return _needs_update; }

    void set_needs_update(bool b) { _needs_update = b; }

    // Define the perspective projection. Note, the vertical fov angle, and height to width ratio is taken as
    // argument.
    void set_proj(f32 zn, f32 zf, f32 y_fov, f32 aspect_h_on_w) {
        _proj_xform = math::perspective_projection(zn, zf, y_fov, 1.0f / aspect_h_on_w);
        _near_plane_y_extent = zn * std::tan(y_fov * 0.5f);
        _far_plane_y_extent = zf * std::tan(y_fov * 0.5f);
        set_needs_update(true);
    }

    void set_ortho_proj(eng::math::OrthoRange x_source,
                        eng::math::OrthoRange x_dest,
                        eng::math::OrthoRange y_source,
                        eng::math::OrthoRange y_dest,
                        eng::math::OrthoRange z_source,
                        eng::math::OrthoRange z_dest) {
        _proj_xform = eng::math::ortho_proj(x_source, x_dest, y_source, y_dest, z_source, z_dest);
    }

    void set_position(fo::Vector3 pos) {
        _eye.position = pos;
        update_view_transform();
    }

    void set_orientation(fo::Quaternion q) {
        _eye.orientation = q;
        update_view_transform();
    }

    void rotate(fo::Quaternion q) {
        eye::rotate(_eye, q);
        update_view_transform();
    }

    // ## Rotate about x, y, z axes of camera frame

    void pitch(f32 radians) {
        eye::pitch(_eye, radians);
        update_view_transform();
    }

    void yaw(f32 radians) {
        eye::yaw(_eye, radians);
        update_view_transform();
    }

    void roll(f32 radians) {
        eye::roll(_eye, radians);
        update_view_transform();
    }

    void move_forward(f32 d) {
        using namespace math;
        _eye.position = _eye.position + d * forward();
        update_view_transform();
    }

    void move_sideways(f32 d) {
        using namespace math;
        _eye.position = _eye.position + d * right();
        update_view_transform();
    }

    void move_upward(f32 d) {
        using namespace math;
        _eye.position = _eye.position + d * up();
        update_view_transform();
    }

    void set_eye(const eye::State &eye) {
        _eye = eye;
        update_view_transform();
    }

    // Set the full local coordinate of the camera. `up` should be a unit length vector.
    void
    set_orthogonal_axes(const fo::Vector3 &target_pos, const fo::Vector3 &current_pos, const fo::Vector3 &up);

    // Use this to look at given target position. The up vector (the camera's local y axis) also needs to be
    // specified. Should be a unit vector.
    void look_at(const fo::Vector3 &target_pos,
                 const fo::Vector3 &current_pos,
                 const fo::Vector3 &up_vector,
                 bool orthogonalize_up = true);

    void update_view_transform() { eye::update_view_transform(_eye, _view_xform); }
};

} // namespace eng
