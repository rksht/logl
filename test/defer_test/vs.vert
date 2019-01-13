#version 430 core

#if !defined(ACTUALLY_USING)
    #ifndef K_NUM_BARS
    #define K_NUM_BARS 500
    #endif
    
    #ifndef MVP_UBO_BINDING
    #define MVP_UBO_BINDING 0
    #endif
    
    #ifndef BAR_TRANSFORMS_UBO_BINDING
    #define BAR_TRANSFORMS_UBO_BINDING 1
    #endif
#endif

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 diffuse;
layout(location = 4) in vec2 st;

layout(binding = MVP_UBO_BINDING, std140) uniform MVP {
    mat4 view;
    mat4 proj;
    vec4 eyepos_world;
} mvp;

struct BarWorldTransform {
    mat4 m;
    mat4 inv_transpose_m;
};

layout(binding = BAR_TRANSFORMS_SSBO_BINDING) buffer BarWorldTransforms {
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
    const mat4 world = arr_bar_transforms[gl_InstanceID].m;
    const mat4 world_inv_trans = arr_bar_transforms[gl_InstanceID].inv_transpose_m;

    mat4 local_to_view = mvp.view * world;

    vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
    
    vs_out.position_world = (world * vec4(position, 1.0)).xyz;
    vs_out.tangent_world = (world * vec4(tangent.xyz, 0.0)).xyz;
    vs_out.normal_world = (world_inv_trans * vec4(normal, 0.0)).xyz;
    vs_out.bitangent_world = (world * vec4(bitangent, 1.0)).xyz;
    vs_out.st = st;

    gl_Position = mvp.proj * mvp.view * world * vec4(position, 1.0);
}
