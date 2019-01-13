#version 420

layout(location = 0) in vec3 vert_pos;
layout(location = 1) in vec3 vert_normal;
layout(location = 2) in vec2 vert_texcoord;

// We don't use a separate model-to-world transform in this example
uniform mat4 view_mat, proj_mat;

out VertOutputBlock {
	vec3 pos_eye;
	vec3 normal_eye;
	vec2 texcoord;
} vs_out;

void main() {
	vs_out.pos_eye = vec3(view_mat * vec4(vert_pos, 1.0));
    vs_out.normal_eye = vec3(view_mat * vec4(vert_normal, 0.0));
    vs_out.texcoord = vert_texcoord;
    gl_Position = proj_mat * vec4(vs_out.pos_eye, 1.0);
}