#pragma once

#include <learnogl/eng.h>

namespace eng {

struct ArcBall {
    fo::Quaternion current_rotation = eng::math::identity_versor;

    Vec2 _drag_start = {};
    Vec2 _drag_end = {};

    const eng::Camera *_camera = nullptr;

    ArcBall(const eng::Camera &camera)
        : _camera(&camera) {}

    void set_drag_start(double screen_x, double screen_y) {
        _drag_start = Vec2((f32)screen_x, (f32)screen_y);
    }

    void set_drag_end(double screen_x, double screen_y) {
        _drag_end = Vec2((f32)screen_x, (f32)screen_y);
        _compute_rotation();
    }

    fo::Quaternion _compute_rotation() {
        using namespace eng::math;
        if (eng::math::square_magnitude(_drag_start - _drag_end) < 0.00001) {
            return;
        }

        auto ray1 = eng::ray_wrt_world(_drag_start);
        auto ray2 = eng::ray_wrt_world(_drag_end);

        auto rotation_axis = normalize(cross(ray1.direction, ray2.direction));
        auto rotation_angle = std::acos(std::clamp(dot(ray1.direction, ray2.direction), 0.0f, 1.0f));

        current_rotation = versor_from_axis_angle(rotation_axis, rotation_angle);
        return current_rotation;
    }

    void reset_rotation() {
        self_.current_rotation = math::identity_versor;
        self_._drag_end = self_._drag_start = Vec2{};
    }
};

} // namespace eng
