#version 410

layout(location = 0) in vec4 vert; // (vec2 pos, vec2 tex), pos in world frame

out vec2 tex2d;

uniform mat4 ortho_proj;


void main() {
	gl_Position = ortho_proj * vec4(vert.xy, 0.0, 1.0);
	tex2d = vert.zw;
}