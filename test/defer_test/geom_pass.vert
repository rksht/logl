#version 430 core

#ifndef ACTUALLY_USING
    #define K_NUM_BARS 500
    #define MVP_UBO_BINDING 0
    #define BAR_TRANSFORMS_UBO_BINDING 1
#endif

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 diffuse;
layout(location = 3) in vec2 st;

layout(binding = MVP_UBO_BINDING, std140) uniform MVP {
    mat4 view;
    mat4 proj;
} mvp;

struct BarWorldTransform {
    mat4 m;
    mat4 inv_transpose_m;
};

layout(binding = BAR_TRANSFORMS_UBO_BINDING) uniform BarWorldTransforms {
    BarWorldTransform arr_bar_transforms[K_NUM_BARS];
};

out VsOut {
    vec2 st;
    vec3 position_world;
    vec3 tangent_world;
    vec3 bitangent_world;
    vec3 normal_world;
} vs_out;

void main() {
    mat4 world = arr_bar_transforms[gl_InstanceID].m;
    mat4 inv_transpose_m = arr_bar_transforms[gl_InstanceID].inv_transpose_m;

    gl_Position = mvp.proj * mvp.view * world * vec4(position, 1.0);

    vs_out.world_position = (world * vec4(position, 1.0)).xyz;
    vs_out.world_normal = normalize((inv_transpose_m * vec4(normal, 0.0)).xyz); // `world` is not orthogonal
    vs_out.diffuse = diffuse;
    vs_out.st = st;
}
