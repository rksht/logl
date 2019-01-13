#version 430 core

#ifndef K_NUM_BARS
#define K_NUM_BARS 500
#endif


layout(location = 0) in vec3 position;

layout(binding = 0) uniform MVP {
    mat4 view;
    mat4 proj;
} mvp;

layout(binding = 2) uniform AABBTransforms {
    mat4 arr_aabb_transforms[K_NUM_BARS];
};

void main() {
    mat4 world = arr_aabb_transforms[gl_InstanceID];
    gl_Position = mvp.proj * mvp.view * world * vec4(position, 1.0);
}
