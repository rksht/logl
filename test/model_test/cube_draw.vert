#version 420

layout(location = 0) in vec3 pos;

layout(std140, binding = 0) uniform CamMatrices {
    mat4 mat_view;
    mat4 mat_proj;
};

uniform mat4 bounding_ob_to_world;

void main() { gl_Position = mat_proj * mat_view * bounding_ob_to_world * vec4(pos, 1.0); }
