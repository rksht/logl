#version 430 core

#ifndef ACTUALLY_USING
    #define Z_NEAR 0.1
    #define TAN_VFOV_DIV_2 0.0
    #define ASPECT_RATIO 4.0/3.0
#endif

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 st;

out QuadVsOut {
    vec2 quad_st;

    // Direction vector from eye origin to near plane corner. We don't care
    // about the z here. Can imagine it as being -Z_NEAR. The fun is in the
    // fragment shader after we interpolate this direction.
    vec3 eye_to_near_plane;
} vs_out;

const vec3 k_near_plane_extent = Z_NEAR * vec3(ASPECT_RATIO * TAN_VFOV_DIV_2 , TAN_VFOV_DIV_2, -1.0);

void main() {
    gl_Position = vec4(position.xy, 1.0, 1.0);

    // The texture coordinates can be scaled and shifted to the range [-1, 1]
    // and then we can get the corner of the near plane this vertex aligns with.
    vs_out.eye_to_near_plane.xy = (2.0 * st - 1.0) * k_near_plane_extent.xy;
    vs_out.eye_to_near_plane.z = k_near_plane_extent.z;

    vs_out.quad_st = st;
}
