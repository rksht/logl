#version 430 core

#include "definitions.inc.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec2 st;

layout(binding = 0, std140) uniform ublock_EyeBlock {
    mat4 view_from_world_xform;
    mat4 clip_from_view_xform;
    vec3 eye_pos;
    float frame_interval;
};

// Per model data (and per mesh since only 1 mesh per model)
layout(binding = 1, std140) uniform ublock_PerObject {
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material;
};

out VsOut {
    vec3 pos_w;
    vec3 normal_w;
    vec3 tangent_w;
    vec2 st;
} vs_out;

void main() {
    mat4 to_world = world_from_local_xform;
    mat4 normal_xform = transpose(inv_world_from_local_xform);

    vs_out.pos_w = x_point(to_world, position);
    vs_out.normal_w = x_vec(normal_xform, normal);
    vs_out.st = st;

    gl_Position = clip_from_view_xform * view_from_world_xform * vec4(vs_out.pos_w, 1.0);
}
