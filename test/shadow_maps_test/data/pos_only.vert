#version 440 core

#include "common_defs.inc.glsl"
#include "definitions.inc.glsl"

layout(location = 0) in vec3 position;

DEFINE_CAMERA_UBLOCK(0, ublock_EyeBlock);

// Per model  data (and per mesh since only 1 mesh per model)
layout(binding = 1, std140) uniform ublock_PerObject
{
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material; // Only using the material field
};

out VsOut { vec3 pos_w; } vs_out;

void main()
{
    mat4 to_world = world_from_local_xform;
    vs_out.pos_w = x_point(to_world, position);
    gl_Position = u_clipFromView * u_viewFromWorld * vec4(vs_out.pos_w, 1.0);
}
