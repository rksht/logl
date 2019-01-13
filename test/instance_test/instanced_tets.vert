#version 410

uniform mat4 view, proj;

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 inst_color; 		// Color of this instance
layout(location = 2) in vec4 inst_scale_trans_ow; 	// x = scale, yzw = translate

out VO {
	vec3 color;
} vo;

void main() {
	// gl_Position = proj * view * inst_ow * vec4(pos, 1.0);
	mat4 ow = mat4(
		vec4(inst_scale_trans_ow.x, 0, 0, 0),
		vec4(0, inst_scale_trans_ow.x, 0, 0),
		vec4(0, 0, inst_scale_trans_ow.x, 0),
		vec4(inst_scale_trans_ow.y, inst_scale_trans_ow.z, inst_scale_trans_ow.w, 1.0)
	);

	gl_Position = proj * view * ow * vec4(pos, 1.0);
	vo.color = inst_color;
}