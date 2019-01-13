#version 420

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 color;

layout(std140, binding = 0) uniform CamMatrices {
    mat4 mat_view;
    mat4 mat_proj;
};

flat out vec3 vo_color;

void main() {
    gl_Position = mat_proj * mat_view * vec4(pos, 1.0);
    gl_PointSize = 5.0;
    vo_color = color;
}
