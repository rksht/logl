#version 430 core

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;

layout(binding = 0, std140) uniform CameraUniform {
  vec4 u_clip_matrix;
  vec4 u_view_matrix;
  vec3 u_camera_position;
};

layout(binding = 1, std140) uniform SingleRigidBody {
  mat4 u_to_world_matrix;
  mat4 u_inv_world_matrix;
};

out Pnuv_VSOut {
  vec4 pos_w;
  vec4 normal_w;
  vec2 uv;
} o_vs;

void main() {
    vec4 pos4 = vec4(i_position, 1.0);
    vec4 pos_w = u_to_world_matrix * pos4;
    vec4 normal_w = transpose(u_inv_world_matrix) * vec4(i_normal, 0.0);

    o_vs.pos_w = pos_w;
    o_vs.normal_w = normal_w;
    o_vs.uv = i_uv;
}
