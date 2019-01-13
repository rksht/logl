#version 430 core

layout(location = 0) in vec3 position;

layout(binding = 0, std140) uniform WVP_Uniform {
	mat4 model_to_world;
	mat4 view;
	mat4 proj;
};

uniform vec4 uniform_color;

out VS_Out {
	vec4 surface_color;
} vs_out;

void main() {
	gl_Position = proj * view * model_to_world * vec4(position, 1.0);
	vs_out.surface_color = uniform_color;
}
