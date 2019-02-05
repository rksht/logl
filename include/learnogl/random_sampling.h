#pragma once

#include <learnogl/math_ops.h>
#include <learnogl/rng.h>

/// Returns a point chosen uniformly randomly from the surface a sphere located at origin. Given `radius` must
/// be positive.
inline fo::Vector3 random_point_on_sphere(f32 radius) {
    using namespace eng::math;
    float y = rng::random(-radius, radius);
    float theta = rng::random(-pi, pi);
    float r = std::sqrt(radius * radius - y * y);
    return fo::Vector3{ r * std::cos(theta), y, r * std::sin(theta) };
}

inline fo::Vector3 random_unit_vector() { return random_point_on_sphere(1.0f); }

/// Returns a point chosen uniformly randomly from the surface of a hemisphere located at the origin. The
/// positive y axis aligns with the direction of the hemisphere.
inline fo::Vector3 random_point_on_hemisphere(f32 radius) {
    using namespace eng::math;

    float y = rng::random(0, radius);
    float theta = rng::random(-pi, pi);
    float r = std::sqrt(radius * radius - y * y);
    return fo::Vector3{ r * std::cos(theta), y, r * std::sin(theta) };
}

/// Returns a point chosen uniformly at random from inside a sphere of given radius.
inline fo::Vector3 random_point_inside_sphere(f32 radius) {
    using namespace eng::math;
    auto on_sphere = random_point_on_sphere(radius);
    float scale = rng::random(-1.0, 1.0);
    return scale * on_sphere;
}

inline fo::Vector2 random_point_inside_circle(f32 radius) {
    float angle = (float)std::sqrt(rng::random(0, 360.0 * 360.0)) * eng::math::one_deg_in_rad;
    return fo::Vector2{ radius * std::cos(angle), radius * std::sin(angle) };
}

