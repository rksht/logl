#version 410

layout(location = 0) in vec3 vert_pos;
layout(location = 1) in vec3 vert_normal;

uniform mat4 model_mat, view_mat, proj_mat;

// out vec3 vert_pos_eye;    // Position in view frame
// out vec3 vert_normal_eye; // Normal in view frame

out VertOutputBlock {
    vec3 pos_eye;
    flat vec3 normal_eye;
} vs_out;

void main() {
    vs_out.pos_eye = vec3(view_mat * model_mat * vec4(vert_pos, 1.0));
    vs_out.normal_eye = vec3(view_mat * model_mat * vec4(vert_normal, 0.0));
    gl_Position = proj_mat * vec4(vs_out.pos_eye, 1.0);
}