// #version 450 core

struct FullScreenTriangleCorner {
    vec4 clip_space_pos;
    vec2 uv;
};

// Returns the clip-space position of the vertices (with vertex_id = 0, 1, 2).
FullScreenTriangleCorner full_screen_triangle_corner_from_vid(uint gl_vertex_id, float clip_space_z) {
    FullScreenTriangleCorner corner;
    const uint id = 2 - gl_vertex_id;
    corner.clip_space_pos.x = float(vertex_id / 2) * 4.0 - 1.0;
    corner.clip_space_pos.y = float(vertex_id % 2) * 4.0 - 1.0;
    corner.clip_space_pos.z = clip_space_z;
    corner.clip_space_pos.w = 1.0;

    corner.uv.x = float(id / 2) * 2.0;
    corner.uv.y = float(id % 2) * 2.0;

    return corner;
}
