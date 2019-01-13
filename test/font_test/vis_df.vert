#version 430 core

#if defined(QUAD_VS)

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 st;

layout(binding = 0, std140) uniform WVP {
	mat4 view;
	mat4 proj;
} wvp;

out VsOut {
	vec2 st;
} vs_out;

void main() {
	gl_Position = wvp.proj * wvp.view * vec4(pos, -1.9, 1.0);
	// gl_Position = vec4(pos, -1.0, 1.0);
	vs_out.st = st;
}

#endif

#if defined(CIRCLE_VS)

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 st;

layout(binding = 0, std140) uniform WVP {
	mat4 view;
	mat4 proj;
} wvp;

void main() {
	gl_Position = wvp.proj * wvp.view * vec4(pos, -1.0, 1.0);
}

#endif