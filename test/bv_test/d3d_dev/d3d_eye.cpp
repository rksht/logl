/// Implements some non-inline functions operating on the eye state.

#include "d3d_eye.h"

namespace d3d_eye {

void update_view_transform(const State &e, xm44 &xform_out) {
    const xmm inv_rot = XMMatrixTranspose(XMMatrixRotationQuaternion(xmload(e.orientation_quat)));

    xmv t = XMVectorNegate(xmload(e.position));
    t = XMVectorSet(XM_XYZ(t), 1.0f);
    t = XMVector4Transform(t, inv_rot);

    const xmm trans = XMMatrixTranslationFromVector(t);
    xmstore(XMMatrixMultiply(inv_rot, trans), xform_out);
}

xmm get_view_to_world_transform(const State &e) {
    const xmm rotation = XMMatrixRotationQuaternion(xmload(e.orientation_quat));
    xmv t = xmload(e.position);
    t = XMVectorSet(XM_XYZ(t), 1.0f);
    const xmm trans = XMMatrixTranslationFromVector(t);
    return XMMatrixMultiply(rotation, trans);
}

} // namespace d3d_eye

void D3DCamera::set_look_at(fxmv camera_pos_w, fxmv target_pos_w, fxmv up_w) {
    // Store the orientation as a quaternion
    auto z = XMVector3Normalize(XMVectorSubtract(target_pos_w, camera_pos_w));
    auto y = XMVector3Normalize(up_w);
    auto x = XMVector3Normalize(XMVector3Cross(y, z));
    y = XMVector3Normalize(XMVector3Cross(z, x)); // Want perpendicular axes

    xmm r;
    r.r[0] = x;
    r.r[1] = y;
    r.r[2] = z;
    r.r[3] = XMVectorSet(0, 0, 0, 1);

    xmstore(camera_pos_w, _eye.position);
    xmstore(XMQuaternionRotationMatrix(r), _eye.orientation_quat);

    xmstore(XMMatrixLookAtLH(camera_pos_w, target_pos_w, up_w), _view_xform);
}

void D3DCamera::set_proj(f32 zn, f32 zf, f32 y_fov, f32 aspect_w_on_h) {
    _y_fov = y_fov;
    _near_z = zn;
    _far_z = zf;
    _aspect_w_on_h = aspect_w_on_h;

    _tan_half_yfov = std::tan(y_fov * 0.5f);
    _tan_half_xfov = aspect_w_on_h * _tan_half_yfov;

    _near_plane_height = (_near_z * _tan_half_yfov) * 2.0f;
    _far_plane_height = (_far_z * _tan_half_yfov) * 2.0f;

    const xmm proj_xform = XMMatrixPerspectiveFovLH(y_fov, _aspect_w_on_h, _near_z, _far_z);
    xmstore(proj_xform, _proj_xform);
    xmstore(XMMatrixInverse(nullptr, proj_xform), _inv_proj_xform);
}

void D3DCamera::update_view_transform() { d3d_eye::update_view_transform(_eye, _view_xform); }

bool handle_camera_input(GLFWwindow *window, D3DCamera &cam, f64 dt) {
    constexpr float eye_linear_vel = 4.0f;
    constexpr float eye_angular_vel = 40.0f * math::one_deg_in_rad;

    const float dist = eye_linear_vel * (f32)dt;
    const float angle = eye_angular_vel * (f32)dt;

    using namespace math;

    bool moved = false;

    if (glfwGetKey(window, GLFW_KEY_W)) {
        cam.move_forward(dist);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_S)) {
        cam.move_forward(-dist);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_A)) {
        cam.move_sideways(-dist);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_D)) {
        cam.move_sideways(dist);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_W) && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) {
        cam.move_upward(dist);
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S) && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) {
        cam.move_upward(dist);
        moved = true;
    }

    // These ones rotate about the eye_up axis (yaw)
    if (glfwGetKey(window, GLFW_KEY_LEFT)) {
        cam.yaw(-angle);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_RIGHT)) {
        cam.yaw(angle);
        moved = true;
    }

    // These keys rotate the eye about the res.eye_right axis  (pitch)
    if (glfwGetKey(window, GLFW_KEY_UP)) {
        cam.pitch(-angle);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_DOWN)) {
        cam.pitch(angle);
        moved = true;
    }
    // These keys rotate the eye about the res.eye_fwd axis (roll)
    if (glfwGetKey(window, GLFW_KEY_J)) {
        cam.roll(angle);
        moved = true;
    }

    if (glfwGetKey(window, GLFW_KEY_L)) {
        cam.roll(-angle);
        moved = true;
    }

    if (moved) {
        cam.update_view_transform();
    }

#if 0
    if (moved) {
        auto x = cam.right();
        auto y = cam.up();
        auto z = cam.forward();
        LOG_F(INFO,
              "\nX(right): [%.3f, %.3f, %.3f],\nY(up): [%.3f, %.3f, %.3f],\nZ(fwd): [%.3f, %.3f, %.3f]",
              XM_XYZ(x),
              XM_XYZ(y),
              XM_XYZ(z));
    }
#endif

    return moved;
}
