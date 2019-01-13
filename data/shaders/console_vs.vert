#version 430 core

#if !defined(VIEWPORT_WIDTH) || !defined(VIEWPORT_HEIGHT)
	#define VIEWPORT_WIDTH 1024
	#define VIEWPORT_HEIGHT 640
#endif

layout(location = 0) in vec2 bbox_corner_pos;
layout(location = 1) in vec2 uv;

out VsOut {
    vec2 uv;
} vs_out;

const mat3 screen2ndc = mat3(
    vec3(2.0 / VIEWPORT_WIDTH, 0.0, 0.0),
    vec3(0.0, -2.0 / VIEWPORT_HEIGHT, 0.0),
    vec3(-1.0, 1.0, 1.0)
);

layout(binding = LINE_CONSTANTS_UBO_BINDING, std140) uniform PerLineConstants {
	vec2 line_offset;
};

void main() {
    vec3 ndc_xyz = screen2ndc * vec3(bbox_corner_pos + line_offset, 1.0);
    ndc_xyz.z = -1.0;

    gl_Position = vec4(ndc_xyz, 1.0);

    vs_out.uv = uv;
}
