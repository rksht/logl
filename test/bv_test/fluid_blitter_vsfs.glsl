#version 430 core

#if defined(DO_VERTEX_SHADER)

layout(location = 0) in vec2 pos;

#include <fs_quad_vs.inc.glsl>

out FullScreenVsOut {
	vec2 uv;
} vo;

// Vertex shader
void main() {
	FullScreenTriangleCorner corner = full_screen_triangle_corner_from_vid(gl_VertexID, -1.0);
	gl_Position = corner.clip_space_pos;
	vo.uv = corner.uv;
}


#elif defined(DO_FRAGMENT_SHADER)

// Fragment shader
void main() {
}


#else

#error "DO_VERTEX_SHADER or DO_FRAGMENT_SHADER needs to be defined"

#endif