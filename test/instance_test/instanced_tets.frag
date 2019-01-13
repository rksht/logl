#version 410

in VO {
	vec3 color;
} vo;

out vec4 color;

void main() {
	color = vec4(vo.color, 1.0);
}