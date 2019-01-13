#include <learnogl/essential_headers.h>

#include <learnogl/bounding_shapes.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/math_ops.h>
#include <learnogl/rng.h>
#include <scaffold/array.h>
#include <scaffold/debug.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdlib.h>
#include <unordered_set>

using namespace fo;
using namespace eng::math;

namespace eng {

static inline Matrix3x3 make_covariance_matrix(const Vector3 *points, uint32_t num_points, Vector3 &mean_out);

PrincipalAxis calculate_principal_axis(const Vector3 *points, uint32_t num_points) {
    Vector3 mean;
    auto cov = make_covariance_matrix(points, num_points, mean);

    // The solver calculates a right-handed eigenbasis. So a rotation transformation from original basis is
    // possible
    SymmetricEigenSolver3x3_Result res;
    eigensolve_sym3x3(cov, res);

    PrincipalAxis pa;

    pa.mean = mean;

    VLOG_F(LOGL_MILD_LOG_CHANNEL,
           R"(
        %s,
        CovarianceMatrix =
            [[%.4f, %.4f, %4f]
             [%.4f, %.4f, %4f]
             [%.4f, %.4f, %4f]]

        Eigvals = [%.3f, %.3f, %.3f]

        Eigvecs = [%.3f, %.3f, %.3f]
                  [%.3f, %.3f, %.3f]
                  [%.3f, %.3f, %.3f]
        )",
           __PRETTY_FUNCTION__,
           XYZ(cov.x),
           XYZ(cov.y),
           XYZ(cov.z),
           XYZ(res.eigenvalues),
           XYZ(res.eigenvectors.x),
           XYZ(res.eigenvectors.y),
           XYZ(res.eigenvectors.z));

    // Normalize the axes to get an orthonormal basis
    pa.axes[0] = normalize(res.eigenvectors.x);
    pa.axes[1] = normalize(res.eigenvectors.y);
    pa.axes[2] = normalize(res.eigenvectors.z);

    // Find the axis along with the points are most skewed. This is the eigenvector with the largest
    // associated eigenvalue.
    pa.axis_with_max_extent = PrincipalAxis::R;
    if (std::abs(res.eigenvalues[PrincipalAxis::R]) < std::abs(res.eigenvalues[PrincipalAxis::S])) {
        pa.axis_with_max_extent = PrincipalAxis::S;
    }

    if (std::abs(res.eigenvalues[PrincipalAxis::S]) < std::abs(res.eigenvalues[PrincipalAxis::T])) {
        pa.axis_with_max_extent = PrincipalAxis::T;
    }

    return pa;
}

Matrix3x3 make_covariance_matrix(const Vector3 *points, uint32_t num_points, Vector3 &mean_out) {
    Matrix3x3 cov;
    float *covp = reinterpret_cast<float *>(&cov);
    Vector3 zero{ 0.f, 0.f, 0.f };

    const Vector3 *points_end = points + num_points;

    const Vector3 mean =
        std::accumulate(points, points_end, zero, [](const Vector3 &a, const Vector3 &b) { return a + b; }) /
        float(num_points);

    mean_out = Vector3{ mean.x, mean.y, mean.z };

    // Keeping the intermediate calculated products in this array before calculating the mean.
    Array<float> scratch(memory_globals::default_allocator(), num_points);
    std::fill(begin(scratch), end(scratch), 0.f);

    auto minus_1 = [&mean](int coord) {
        return [&mean, coord](const Vector3 &point) -> float {
            const float *p = reinterpret_cast<const float *>(&point);
            const float *m = reinterpret_cast<const float *>(&mean);
            const float d = p[coord] - m[coord];
            return d * d;
        };
    };

    auto minus_2 = [&mean](int coord1, int coord2) {
        return [&mean, coord1, coord2](const Vector3 &point) -> float {
            const float *p = reinterpret_cast<const float *>(&point);
            const float *m = reinterpret_cast<const float *>(&mean);
            return (p[coord1] - m[coord1]) * (p[coord2] - m[coord2]);
        };
    };

    auto mean_scratch = [&scratch]() {
        return std::accumulate(begin(scratch), scratch.end(), 0.0f) / float(size(scratch));
    };

    Index2D<int32_t> idx{ 3 };

    std::transform(points, points_end, begin(scratch), minus_1(0));
    covp[idx(0, 0)] = mean_scratch();

    std::transform(points, points_end, begin(scratch), minus_1(1));
    covp[idx(1, 1)] = mean_scratch();

    std::transform(points, points_end, begin(scratch), minus_1(2));
    covp[idx(2, 2)] = mean_scratch();

    std::transform(points, points_end, begin(scratch), minus_2(0, 1));
    float cov_01 = mean_scratch();

    std::transform(points, points_end, begin(scratch), minus_2(0, 2));
    float cov_02 = mean_scratch();

    std::transform(points, points_end, begin(scratch), minus_2(1, 2));
    float cov_12 = mean_scratch();

    covp[idx(0, 1)] = cov_01;
    covp[idx(1, 0)] = cov_01;
    covp[idx(0, 2)] = cov_02;
    covp[idx(2, 0)] = cov_02;
    covp[idx(1, 2)] = cov_12;
    covp[idx(2, 1)] = cov_12;

    return cov;
}

BoundingRect create_bounding_rect(const PrincipalAxis &pa, const Vector3 *points, uint32_t num_points) {
    const Vector3 r_axis = pa.axes[PrincipalAxis::R];
    const Vector3 s_axis = pa.axes[PrincipalAxis::S];
    const Vector3 t_axis = pa.axes[PrincipalAxis::T];

    // Set up the plane normals first
    BoundingRect bb = { // The six planes
                        { Vector4{ r_axis, 0 },
                          Vector4{ -r_axis, 0 },
                          Vector4{ s_axis, 0 },
                          Vector4{ -s_axis, 0 },
                          Vector4{ t_axis, 0 },
                          Vector4{ -t_axis, 0 } },
                        // half extent
                        Vector3{},
                        // center
                        Vector3{}
    };

    const auto find_min_max = [&pa, points, num_points](const Vector3 &axis) {
        Vector3 min_point = points[0] - pa.mean;
        Vector3 max_point = points[0] - pa.mean;
        float min_proj = dot(min_point, axis);
        float max_proj = min_proj;

        for (uint32_t i = 1; i < num_points; ++i) {
            const Vector3 diff = points[i] - pa.mean;
            float proj = dot(diff, axis);
            if (proj < min_proj) {
                min_proj = proj;
                min_point = points[i];
            }
            if (proj > max_proj) {
                max_proj = proj;
                max_point = points[i];
            }
        }

        return std::make_pair(min_proj, max_proj);
    };

    const auto r_minmax = find_min_max(pa.axes[0]);
    const auto s_minmax = find_min_max(pa.axes[1]);
    const auto t_minmax = find_min_max(pa.axes[2]);
    const float rmin = r_minmax.first;
    const float rmax = r_minmax.second;
    const float smin = s_minmax.first;
    const float smax = s_minmax.second;
    const float tmin = t_minmax.first;
    const float tmax = t_minmax.second;

    // Set the D part of the planes
    bb.planes[BoundingRect::R_POS].w = -rmin;
    bb.planes[BoundingRect::R_NEG].w = rmax;
    bb.planes[BoundingRect::S_POS].w = -smin;
    bb.planes[BoundingRect::S_NEG].w = smax;
    bb.planes[BoundingRect::T_POS].w = -tmin;
    bb.planes[BoundingRect::T_NEG].w = tmax;

    // Now we obtain the half_extent of the rectangle
    bb.center = pa.mean +
                ((rmin + rmax) * pa.axes[0] + (smin + smax) * pa.axes[1] + (tmin + tmax) * pa.axes[2]) / 2.0f;
    bb.half_extent = Vector3{ rmax - rmin, smax - smin, tmax - tmin } / 2.0f;

    return bb;
}

static BoundingSphere sphere_of_sphere_and_point(const BoundingSphere &current_sphere, const Vector3 &point) {
    auto d = point - current_sphere.center;
    f32 dist2 = square_magnitude(d);
    if (dist2 > current_sphere.radius * current_sphere.radius) {
        f32 dist = std::sqrt(dist2);
        f32 new_radius = (current_sphere.radius + dist) * 0.5f;
        f32 k = (new_radius - current_sphere.radius) / dist;
        return BoundingSphere{ current_sphere.center + k * d, new_radius };
    }
    return current_sphere;
}

BoundingSphere
create_bounding_sphere(const PrincipalAxis &pa, const Vector3 *positions, uint32_t num_points) {
    assert(num_points > 1);

    uint32_t min_point_index = 0;
    uint32_t max_point_index = 0;

    float cur_max_extent_sq = dot(positions[0], pa.axes[pa.axis_with_max_extent]);
    float cur_min_extent_sq = cur_max_extent_sq;

    for (uint32_t i = 1; i < num_points; ++i) {
        const Vector3 &pos = positions[i];
        float extent_sq = dot(pos, pa.axes[pa.axis_with_max_extent]);

        if (extent_sq > cur_max_extent_sq) {
            max_point_index = i;
            cur_max_extent_sq = extent_sq;
        } else if (extent_sq < cur_min_extent_sq) {
            min_point_index = i;
            cur_min_extent_sq = extent_sq;
        }
    }

    Vector3 center = 0.5f * (positions[min_point_index] + positions[max_point_index]);
    float radius = magnitude(positions[min_point_index] - center);

    std::unordered_set<uint32_t> points_checked;
    points_checked.reserve(num_points);
    points_checked.insert(min_point_index);
    points_checked.insert(max_point_index);

    for (uint32_t i = 0; i < num_points; ++i) {
        if (points_checked.find(i) != points_checked.end()) {
            continue;
        }
        points_checked.insert(i);

        const Vector3 &pos = positions[i];
        const float dist_from_center_sq = square_magnitude(pos - center);
        if (dist_from_center_sq > radius * radius) {
            const Vector3 center_to_pos = pos - center;
            const float dist = std::sqrt(dist_from_center_sq);
            const Vector3 opposite_point = center - std::sqrt(radius) * (center_to_pos / dist);

            center = 0.5f * (opposite_point + pos);
            radius = dist;
        }
    }

    return BoundingSphere{ center, radius };
}

AABB calculate_AABB(const fo::Vector3 *positions, uint32_t num_points) {
    assert(num_points != 0);

    Vector3 min = positions[0];
    Vector3 max = positions[0];

    for (uint32_t i = 1; i < num_points; ++i) {
        const Vector3 &p = positions[i];

        if (max.x < p.x) {
            max.x = p.x;
        }

        if (max.y < p.y) {
            max.y = p.y;
        }

        if (max.z < p.z) {
            max.z = p.z;
        }

        if (min.x > p.x) {
            min.x = p.x;
        }

        if (min.y > p.y) {
            min.y = p.y;
        }

        if (min.z > p.z) {
            min.z = p.z;
        }
    }
    return AABB{ min, max };
}

BoundingSphere
create_bounding_sphere_iterative(const PrincipalAxis &pa, Vector3 *positions, uint32_t num_points) {
    auto bs = create_bounding_sphere(pa, positions, num_points);

    constexpr u32 num_iterations = 32;

    auto bs2 = bs;

    for (u32 k = 0; k < num_iterations; ++k) {
        // Reduce radius a little
        bs2.radius = bs2.radius * 0.95f;

        for (u32 i = 0; i < num_points; ++i) {
            // Choose a random point, and bound it if not already
            u32 point = (u32)rng::random(i + 1, num_points);
            std::swap(positions[i], positions[point]);
            bs2 = sphere_of_sphere_and_point(bs2, positions[i]);
        }

        if (bs2.radius < bs.radius) {
            // DLOG_F(INFO, "%s - Iteration - %u - Tighter", __FUNCTION__, k);
            bs = bs2;
        }
    }

    return bs;
}

} // namespace eng
