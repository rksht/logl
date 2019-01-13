#pragma once

#include "d3d11_misc.h"

/// Provides an eye and some functions that control its orientation_quat and position, and set up a
/// corresponding view transform. The associated view transform must be updated after calling ny function that
/// modifies the eye state. These functions take a non-const reference to the eye state.
namespace d3d_eye {

/// Stores the current eye basis' position and orientation_quat wrt world basis
struct State {
    xm4 orientation_quat; // Current orientation wrt the world basis
    xm3 position;         // Current position of eye
};

/// Creates an eye basis where the 'forward' vector of the eye in world basis becomes the negative z axis
inline State toward_negz(float zpos = 0.2f) { return { xm4(0.0f, 0.0f, 0.0f, 1.0f), xm3(0.0f, 0.0f, zpos) }; }

/// A bird's eye view
inline State toward_negy(float zpos = 0.0) {
    auto quat = XMQuaternionRotationAxis(xm_unit_x(), 270.0f * math::one_deg_in_rad);
    State s;
    XMStoreFloat4(&s.orientation_quat, quat);
    s.position.x = 0.0f;
    s.position.y = 0.0f;
    s.position.z = zpos;
    return s;
}

/// Updates the given view matrix to be in accordance with the eye's state.
void update_view_transform(const State &e, xm44 &xform_out);

/// Sets the given matrix to the inverse of the view transform.
xmm get_view_to_world_transform(const State &e);

/// Rotates the eye using the given versor
inline void rotate(State &e, fxmv q) {
    auto new_quat = XMQuaternionMultiply(xmload(e.orientation_quat), q);
    new_quat = XMQuaternionNormalize(new_quat);
    xmstore(new_quat, e.orientation_quat);
}

/// Returns the forward direction of the eye (direction the eye is looking at)
inline fxmv forward(const State &e) {
    fxmv f = XMVector3Rotate(xm_unit_z(), XMLoadFloat4(&e.orientation_quat));
    return f;
}

/// Returns the upward direction of the eye (direction the eye is looking at)
inline fxmv up(const State &e) { return XMVector3Rotate(xm_unit_y(), XMLoadFloat4(&e.orientation_quat)); }

/// Returns the right hand direction of the eye
inline fxmv right(const State &e) { return XMVector3Rotate(xm_unit_x(), XMLoadFloat4(&e.orientation_quat)); }

/// Rotate the eye about its current local z-axis(the `forward` direction) by the given angle
inline void roll(State &e, float radians) {
    fxmv axis = XMVector3Rotate(xm_unit_z(), XMLoadFloat4(&e.orientation_quat));
    rotate(e, XMQuaternionRotationAxis(axis, radians));
}

/// Rotate the eye about its current local y-axis (the 'up' direction) by the given angle
inline void yaw(State &e, float radians) {
    fxmv axis = XMVector3Rotate(xm_unit_y(), XMLoadFloat4(&e.orientation_quat));
    rotate(e, XMQuaternionRotationAxis(axis, radians));
}

/// Rotate the eye about its current local x-axis (the 'right' direction) by the given angle
inline void pitch(State &e, float radians) {
    fxmv axis = XMVector3Rotate(xm_unit_x(), XMLoadFloat4(&e.orientation_quat));
    rotate(e, XMQuaternionRotationAxis(axis, radians));
}

} // namespace d3d_eye

struct D3DCamera {
    d3d_eye::State _eye;
    xm44 _view_xform; // Needs to be updated everytime camera moves or rotates
    xm44 _proj_xform; // Needs to be updated if viewport dimensions or new/far plane changes
    xm44 _inv_proj_xform;

    // These properties don't usually change once set.
    GETONLY(f32, near_z);
    GETONLY(f32, far_z);
    GETONLY(f32, aspect_w_on_h);
    GETONLY(f32, y_fov);
    GETONLY(f32, near_plane_height);
    GETONLY(f32, far_plane_height);

    GETONLY(f32, tan_half_xfov);
    GETONLY(f32, tan_half_yfov);
    // ^ Tan of these two are stored because we need them sometimes for constructing the

    // ## Getters

    xmv position() const { return xmload(_eye.position); }
    xmv orientation() const { return xmload(_eye.orientation_quat); }
    xmv forward() const { return d3d_eye::forward(_eye); }
    xmv up() const { return d3d_eye::up(_eye); }
    xmv right() const { return d3d_eye::right(_eye); }
    xmm view_xform() const { return xmload(_view_xform); }
    xmm proj_xform() const { return xmload(_proj_xform); }
    xmm view_proj_xform() const { return view_xform() * proj_xform(); }

    // Set the world space position of the camera
    void set_position(fxmv position) { xmstore(position, _eye.position); }

    // Define the world to view matrix
    void set_look_at(fxmv camera_pos_w, fxmv target_pos_w, fxmv up_w);

    // Define the perspective projection. Note, the vertical fov angle, and width to height ratio is taken as
    // argument.
    void set_proj(f32 zn, f32 zf, f32 y_fov, f32 aspect_w_on_h);

    // ## Rotate about x, y, z axes of camera frame

    void pitch(f32 radians) { d3d_eye::pitch(_eye, radians); }
    void yaw(f32 radians) { d3d_eye::yaw(_eye, radians); }
    void roll(f32 radians) { d3d_eye::roll(_eye, radians); }

    void move_forward(f32 d) {
        xmstore(XMVectorMultiplyAdd(XMVectorReplicate(d), forward(), xmload(_eye.position)), _eye.position);
    }

    void move_sideways(f32 d) {
        xmstore(XMVectorMultiplyAdd(XMVectorReplicate(d), right(), xmload(_eye.position)), _eye.position);
    }

    void move_upward(f32 d) {
        xmstore(XMVectorMultiplyAdd(XMVectorReplicate(d), up(), xmload(_eye.position)), _eye.position);
    }

    void update_view_transform();
};

bool handle_camera_input(GLFWwindow *window, D3DCamera &cam, f64 dt);
