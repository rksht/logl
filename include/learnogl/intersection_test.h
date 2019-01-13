#pragma once

#include <learnogl/math_ops.h>
#include <learnogl/kitchen_sink.h>

namespace eng {

// Returns the closest point on given AABB from the given point p.
REALLY_INLINE fo::Vector3 closest_point_in_aabb(const fo::Vector3 &p, const fo::AABB &aabb) {
    simd::Vector4 p_xmm(p.x, p.y, p.z, 1.0f);
    simd::Vector4 tmp = _mm_max_ps(p_xmm, simd::Vector4(aabb.min, 1.0f));
    tmp = _mm_min_ps(tmp, simd::Vector4(aabb.max, 1.0f));
    return fo::Vector3(tmp);
}

REALLY_INLINE fo::Vector3 closest_point_in_obb(const fo::Vector3 &p, const fo::OBB &obb) {
    // Transform point to obb's local coordinates.
    simd::Vector4 cp = simd::sub(simd::Vector4(p, 0.0f), simd::Vector4(obb.center, 0.0f));

    simd::Vector4 local_x = simd::Vector4(obb.xyz[0], 0.0f);
    simd::Vector4 local_y = simd::Vector4(obb.xyz[1], 0.0f);
    simd::Vector4 local_z = simd::Vector4(obb.xyz[2], 0.0f);

    simd::Vector4 p_wrt_obb(simd::dot(cp, local_x).x(),
                            simd::dot(cp, local_y).x(),
                            simd::dot(cp, local_z).z(),
                            0.0f); // < Can this extraction be improved?

    // Get the closest point
    simd::Vector4 obb_max(obb.he, 0.0f);
    simd::Vector4 obb_min = simd::negate(obb_max);
    simd::Vector4 closest = _mm_max_ps(p_wrt_obb, obb_min);
    closest = _mm_min_ps(closest, obb_max);

    // Transform closest point back to world coordinates
    closest = simd::add(simd::add(simd::mul(closest.x(), local_x), simd::mul(closest.y(), local_y)),
                        simd::mul(closest.z(), local_z));
    return (fo::Vector3)closest;
}

// Returns true if given sphere intersects given plane.
REALLY_INLINE bool
test_sphere_plane(const fo::Vector3 &sphere_center, float sphere_radius, const fo::Vector4 plane) {
    const float distance = math::dot(fo::Vector4(sphere_center, 1.0f), plane);
    return std::abs(distance) <= sphere_radius;
}

// Returns true if the two given spheres are intersecting one another.
REALLY_INLINE bool test_sphere_sphere(const fo::Vector3 &sphere0_center,
                                      float sphere0_radius,
                                      const fo::Vector3 &sphere1_center,
                                      float sphere1_radius) {
    using namespace math;
    float rsum = sphere0_radius + sphere1_radius;
    return square_magnitude(sphere0_center - sphere1_center) <= rsum * rsum;
}

REALLY_INLINE bool test_sphere_obb(const fo::Vector3 &sphere_center, float radius, const fo::OBB &obb) {
    using namespace math;
    fo::Vector3 closest = closest_point_in_obb(sphere_center, obb);
    return radius * radius >= square_magnitude(sphere_center - closest);
}

} // namespace eng
