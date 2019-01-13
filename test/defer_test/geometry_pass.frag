#version 430 core

#ifndef ACTUALLY_USING
    #define POSITION_COLOR_ATTACHMENT 0
    #define DIFFUSE_COLOR_ATTACHMENT 1
    #define NORMAL_COLOR_ATTACHMENT 2
    #define TCOORD_COLOR_ATTACHMENT 3
#endif

in VertexAttributes {
    vec3 world_position;
    vec3 world_normal;
    vec3 diffuse;
    vec2 st;
} vout;

layout(location = POSITION_COLOR_ATTACHMENT) out vec3 world_position_out;
layout(location = DIFFUSE_COLOR_ATTACHMENT) out vec3 diffuse_out;
layout(location = NORMAL_COLOR_ATTACHMENT) out vec3 world_normal_out;
layout(location = TCOORD_COLOR_ATTACHMENT) out vec3 st_out;

void main() {
    world_position_out = vout.world_position;
    world_normal_out = vout.world_normal;
    diffuse_out = vout.diffuse;
    st_out = vec3(vout.st, 0.0);
}
