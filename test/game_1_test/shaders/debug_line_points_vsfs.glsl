#version 430 core

#if defined(DO_VS)

#ifndef NUM_BILLBOARDS
#define NUM_BILLBOARDS 400
#endif

struct TextBillboardInfo {
    vec3 center_pos;
    vec2 extent_xy;
};

layout(binding = 0, std140) uniform CameraUB {
    mat4 view_from_world;
    mat4 clip_from_view;
};

layout(binding = RESERVED_UBLOCK_BINDPOINT, std140) uniform TextBillboardInfo_ArrayUB {
    TextBillboardInfo u_text_billboard_info[NUM_BILLBOARDS];
};

void main() {
    const uint vid = gl_VertexID;
    const uint billboard_id = vid / 6;
    const uint corner_id = vid % 6;

    TextBillboardInfo billboard_info = u_text_billboard_info[billboard_id];

    vec2 v;
    v.x = 2u <= corner_id && corner_id <= 4 ? 1.0 : -1.0;
    v.y = 1u <= corner_id && corner_id <= 3 ? -1.0 : 1.0;

    v *= billboard_info.extent_xy;

    vec4 corner_pos_wrt_view_space = vec4(v, 0.0, 0.0) + view_from_world * vec4(billboard_info.center_pos, 1.0);
    gl_Position = clip_from_view * corner_pos_wrt_view_space;
}

#elif defined(DO_FS)

out vec4 frag_color;

void main() {
    frag_color = vec4(1.0, 0.0, 0.0, 0.3);
}

#else

#error "DO_VS xor DO_FS needs to be defined"

#endif
