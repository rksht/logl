#version 410

layout(location = 0) in vec3 pos;

uniform float radius;
uniform vec3 translate;
uniform mat4 mat_view;
uniform mat4 mat_proj;

void main() {
    // clang-format off
    mat4 mat_model = mat4(
        vec4(radius, 0.0, 0.0, 0.0),
        vec4(0.0, radius, 0.0, 0.0),
        vec4(0.0, 0.0, radius, 0.0),
        vec4(translate, 1.0)
    );
    // clang-format on

    gl_Position = mat_proj * mat_view * mat_model * vec4(pos, 1.0);
}
