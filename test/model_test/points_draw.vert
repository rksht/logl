#version 420

layout(location = 0) in vec3 pos;

uniform vec3 eye_pos;

layout(std140, binding = 0) uniform CamMatrices {
    mat4 mat_view;
    mat4 mat_proj;
};

const float MAX_DISTANCE = 2.0;
const float MAX_POINT_SIZE = 10.0;

void main() {
    gl_Position = mat_proj * mat_view * vec4(pos, 1.0);

    float distance_from_eye = clamp(length(eye_pos - pos), 0.0, MAX_DISTANCE);

    float factor = MAX_DISTANCE / distance_from_eye;

    gl_PointSize =  factor * MAX_POINT_SIZE;
}
