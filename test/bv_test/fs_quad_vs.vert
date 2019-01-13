#version 430

#include <common_defs.inc.glsl>

out VsOut {
    vec2 uv;
} vsout;

void ENTRY() {
    const uint id = 2 - gl_VertexID;
    gl_Position.x = float(id / 2) * 4.0 - 1.0;
    gl_Position.y = float(id % 2) * 4.0 - 1.0;
    gl_Position.z = -1.0;
    gl_Position.w = 1.0;

    vsout.uv.x = float(id / 2) * 2.0;
    vsout.uv.y = float(id % 2) * 2.0;
}
