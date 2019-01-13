#version 430 core

layout(location = 0) in vec2 position_ndc;
layout(location = 1) in vec2 st;

void main() {
	gl_Position = vec4(position_ndc, -1.0, 1.0);
}
