#version 430 core

#include "definitions.inc.glsl"

layout(location = 0) in vec3 position;

// Per model  data (and per mesh since only 1 mesh per model)
layout(binding = PER_OBJECT_UBLOCK_BINDING, std140) uniform ublock_PerObject {
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material;
};

layout(binding = CAMERA_ETC_UBLOCK_BINDING, std140) uniform ublock_EyeBlock {
    mat4 view_from_world_xform;
    mat4 clip_from_view_xform;
    vec3 eye_pos;
    float frame_interval;
};

out VsOut {
    vec3 pos_w;
} vs_out;

void main() {
	mat4 to_world = world_from_local_xform;
	vs_out.pos_w = x_point(to_world, position);
	gl_Position = clip_from_view_xform * view_from_world_xform * vec4(vs_out.pos_w, 1.0);
}
