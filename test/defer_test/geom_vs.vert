#version 430 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 diffuse;
layout(location = 4) in vec2 st;

layout(binding = VIEWPROJ_UBO_BINDING, std140) uniform ViewProjBlock {
    mat4 view;
    mat4 proj;
    vec4 eyepos_world;
} vp;

struct MatrixTransform {
    mat4 m;
    mat4 inv_m;
};

layout(binding = SPHERE_XFORMS_SSBO_BINDING) buffer SphereWorldTransforms {
    MatrixTransform arr_matrix_xforms[NUM_SPHERES];
};

uniform mat4 rotation_matrix;
uniform int object_number;

out VsOut {
    vec2 st;
    vec3 position_world;
    vec3 tangent_world;
    vec3 bitangent_world;
    vec3 normal_world;
} vs_out;

MatrixTransform get_object_transform() {
#if INSTANCED
    return arr_matrix_xforms[gl_InstanceID];
#else
    return arr_matrix_xforms[object_number];
#endif
}

void main() {
    MatrixTransform xform = get_object_transform();

    mat4 world = xform.m;
    world = rotation_matrix * world;

    const mat4 world_inv_trans = transpose(xform.inv_m);

    mat4 local_to_view = vp.view * world;

    vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;

    vs_out.position_world = (world * vec4(position, 1.0)).xyz;
    vs_out.tangent_world = (world * vec4(tangent.xyz, 0.0)).xyz;
    vs_out.normal_world = (world_inv_trans * vec4(normal, 0.0)).xyz;
    vs_out.bitangent_world = (world * vec4(bitangent, 1.0)).xyz;
    vs_out.st = st;

    gl_Position = vp.proj * vp.view * world * vec4(position, 1.0);
}