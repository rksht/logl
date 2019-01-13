#version 430 core

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 st;


const mat3 screen2ndc = mat3(
    vec3(2.0, 0.0, 0.0),
    vec3(0.0, -2.0, 0.0),
    vec3(-1.0, 1.0, 1.0)
);

out VsOut {
    vec2 st;
} vs_out;

void main() {
    vec3 ndc_xyz = screen2ndc * vec3(pos, 1.0);
    ndc_xyz.z = -1.0;
    gl_Position = vec4(ndc_xyz, 1.0);

    vs_out.st = st;
}
