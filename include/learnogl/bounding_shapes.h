// Calculation of bounding volumes (not just bounding box)
#pragma once

#include <scaffold/math_types.h>

namespace eng {

// Principle Axis is represented by an orthonormal basis.
struct PrincipalAxis {
    // Giving each axis a name
    enum AxisName { R = 0, S = 1, T = 2 };

    fo::Vector3 mean;
    fo::Vector3 axes[3];
    AxisName axis_with_max_extent;
};

PrincipalAxis calculate_principal_axis(const fo::Vector3 *points, uint32_t num_points);

// A bounding 3D rectangle is first calculated as a collection of 6 planes
struct BoundingRect {
    // The plane equations in <N, D> form. The normals of the plane point 'outwards' from the box.
    fo::Vector4 planes[6];
    // The distances from center to face along the R, S, T directions.
    fo::Vector3 half_extent;
    // The center of the rect
    fo::Vector3 center;

    // We are giving planes IDs according to their normal. This helps in looping.
    enum { R_POS = 0, R_NEG, S_POS, S_NEG, T_POS, T_NEG };
};

BoundingRect create_bounding_rect(const PrincipalAxis &pa, const fo::Vector3 *points, uint32_t num_points);

struct BoundingSphere {
    fo::Vector3 center;
    float radius;

    operator fo::Vector4() { return fo::Vector4(center, radius); }
};

BoundingSphere
create_bounding_sphere(const PrincipalAxis &pa, const fo::Vector3 *positions, uint32_t num_points);

// Create a bounding sphere with iterative improvement. Will swap given points in place. Results in a much
// tighter sphere.
BoundingSphere
create_bounding_sphere_iterative(const PrincipalAxis &pa, fo::Vector3 *positions, uint32_t num_points);

// AABB is already defined in scaffold/math_types.h. This is just a function for creating one from a list of
// positions.
fo::AABB calculate_AABB(const fo::Vector3 *positions, uint32_t num_points);

} // namespace eng
