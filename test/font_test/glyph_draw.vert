#version 430 core

layout(location = 0) in vec2 quad_vertex_ndc;
layout(location = 1) in vec2 quad_glyph_st;

out GlyphVsOut {
    vec2 st;
    vec2 pos;
} vs_out;

layout(binding = 1, std140) uniform TexCoords {
	vec4 corner_st[2];
};

void main() {
    gl_Position = vec4(quad_vertex_ndc, -1.0, 1.0);

    vec2 st;

    if (gl_VertexID == 0) {
    	st = corner_st[0].xy;
    } else if (gl_VertexID == 1) {
    	st = corner_st[0].zw;
    } else if (gl_VertexID == 2) {
    	st = corner_st[1].xy;
    } else {
    	st = corner_st[1].zw;
    }

    vs_out.pos = (quad_vertex_ndc.xy + 1.0f) / 2.0f;
    // vs_out.st = vec2(float(gl_VertexID));
    vs_out.st = st;
}
