#version 430 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec2 st;

struct Material {
    vec4 diffuse_albedo;
    vec3 fresnel_R0;
    float shininess;
};

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
    vec3 normal_w;
    vec3 tangent_w;
    // vec3 bitangent_w;
    vec2 st;
} vs_out;

vec3 x_point(mat4 m, vec3 p) { return (m * vec4(p, 1.0)).xyz; }
vec3 x_vec(mat4 m, vec3 v) { return (m * vec4(v, 0.0)).xyz; }

void main() {
    mat4 to_world = world_from_local_xform;
    mat4 normal_xform = transpose(inv_world_from_local_xform);

    vs_out.pos_w = x_point(to_world, position);
    vs_out.normal_w = x_vec(normal_xform, normal); // Wrong if not an orthogonal matrix

    // vs_out.tangent_w = x_vec(to_world, tangent.xyz);

    // vs_out.bitangent_w = cross(normal, tangent.xyz) * tangent.w;
    // vs_out.bitangent_w = x_vec(to_world, vs_out.bitangent_w);

    vs_out.st = st;

    gl_Position = clip_from_view_xform * view_from_world_xform * vec4(vs_out.pos_w, 1.0);
}
