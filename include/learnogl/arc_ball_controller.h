#pragma once

#include <learnogl/gl_misc.h>

struct ArcBall {
    fo::Quaternion current_rotation = eng::math::identity_versor;
    f32 sphere_radius = 1.0f; // Sphere radius in model space

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

    void _compute_rotation() {
        using namespace eng::math;
        if (eng::math::square_magnitude(_drag_start - _drag_end) < 0.00001) {
            return;
        }
    }
};
