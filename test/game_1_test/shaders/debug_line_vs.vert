#version 430 core

/*
    __macro__

    CAMERA_UBO_BINDPOINT = int
*/

layout(location = 0) uniform vec2 cell_size_xy;
layout(location = 1) uniform vec2 length_xy;
layout(location = 2) uniform ivec2 line_count_xy;
layout(location = 3) uniform vec2 min_vertex_position;
layout(location = 4) uniform int horizontal_or_vertical;

layout(binding = CAMERA_UBO_BINDPOINT, std140) uniform CameraUB {
    mat4 view_from_world;
    mat4 clip_from_view;
};

#if 1
void main() {
    uint vid = gl_VertexID;

    // const uint num_lines = line_count_xy.x + line_count_xy.y;

    vec2 dir_choose_0;
    vec2 dir_choose_1;

    if (horizontal_or_vertical == 1) {
        dir_choose_0 = vec2(1.0, 0.0);
        dir_choose_1 = vec2(0.0, 1.0);
    } else {
        dir_choose_0 = vec2(0.0, 1.0);
        dir_choose_1 = vec2(1.0, 0.0);
    }

    uint line_number = vid / 2;

    vec2 pos = min_vertex_position + float(line_number) * (cell_size_xy * dir_choose_0);
    if (vid % 2 != 0) {
        pos += length_xy * dir_choose_1;
    }

    gl_Position = clip_from_view * view_from_world * vec4(pos, 0.0, 1.0);
}

#else

void main() {
    uint vid = gl_VertexID;

    vec4 pos;

    if (vid == 0) {
        pos = vec4(-10.0, 0.0, 0.0, 1.0);
    } else if (vid == 1) {
        pos = vec4(10.0, 0.0, 0.0, 1.0);
    } else {
        pos = vec4(0.0, 10.0, 0.0, 1.0);
    }

    gl_Position = clip_from_view * view_from_world * pos;
}

#endif
