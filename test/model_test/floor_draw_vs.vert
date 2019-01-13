#version 430 core

#ifndef FLOOR_SQUARE_SIDE
#define FLOOR_SQUARE_SIDE 30
#endif

// Bind space position
layout(location = 0) in vec3 position;

struct Eye {
    vec4 orientation;
    vec3 position;
};

layout(binding = 0, std140) uniform ViewProj {
    mat4 view;
    mat4 proj;
    Eye eye_world;
} viewproj;

out VsOut {
    vec3 color;
} vs_out;


void main() {
    vs_out.color = (viewproj.proj * viewproj.view * vec4(position, 1.0)).xyz;
    vs_out.color /= FLOOR_SQUARE_SIDE;
    gl_Position = viewproj.proj * viewproj.view * vec4(position, 1.0);
}
