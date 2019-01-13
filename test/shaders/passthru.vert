#version 410

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

out vec3 vert_color;

uniform mat4 ob_to_world, world_to_eye, eye_to_clip;

void main() {
	gl_Position = eye_to_clip * world_to_eye * ob_to_world * vec4(position, 1.0);
	vert_color = color;
}