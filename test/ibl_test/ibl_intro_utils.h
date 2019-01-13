#pragma once

#include <learnogl/gl_misc.h>

using namespace fo;
using namespace math;

// All work related to 'setting up' the gpu resources and shaders are defined here.

struct SquareFace {
    // Two triangles making the square face

    u16 bot_right[3] = {};
    u16 top_left[3] = {};

    SquareFace() = default;

    constexpr SquareFace(u16 bl, u16 br, u16 tr, u16 tl) {
        bot_right[0] = bl;
        bot_right[1] = br;
        bot_right[2] = tr;
        top_left[0] = bl;
        top_left[1] = tr;
        top_left[2] = tl;
    }
};

static_assert(sizeof(SquareFace) == sizeof(u16) * 6, "");

constexpr Vector3 CUBE_POINTS[] = { { -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, -1.0f },
                                    { -1.0f, 1.0f, -1.0f },  { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f },
                                    { 1.0f, 1.0f, 1.0f },    { -1.0f, 1.0f, 1.0f } };

// Only the front face is textured using the glyph.
constexpr Vector2 CUBE_TCOORDS[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
                                     { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f } };

constexpr SquareFace CUBE_INDICES[] = { SquareFace(0, 1, 2, 3), SquareFace(1, 5, 6, 2),
                                        SquareFace(5, 4, 7, 6), SquareFace(4, 0, 3, 7),
                                        SquareFace(3, 2, 6, 7), SquareFace(5, 4, 1, 0) };

struct CubeGlobal {
    GLuint vbo = 0;
    GLuint ebo = 0;
    u32 vbo_bytes = 0;

    u32 pos_start_offset = 0;
    u32 tcoords_start_offset = 0;
    u32 normals_start_offset = 0;
    

    u32 num_indices = 0;

    fo::Array<Vector3> cube_points { memory_globals::default_allocator() };
    fo::Array<Vector2> cube_tcoords { memory_globals::default_allocator() };
    fo::Array<Vector3> cube_normals { memory_globals::default_allocator() };

    fo::Array<Vector4> cube_tangents { memory_globals::default_allocator() }; // Not using currently

    fo::Array<u16> cube_indices { memory_globals::default_allocator() };

    static VaoFormatDesc get_vao_format() {
        // Observe the relative_offset of each being 0 since we're not using a coalesced vertex structure
        auto pos_format = VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, 0);
        auto normal_format = VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, 0);
        auto tcoord_format = VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, 0);

        return VaoFormatDesc::from_attribute_formats({ pos_format, normal_format, tcoord_format });
    }
};

// Must have GL initialized
void init_cube_global(CubeGlobal &c) {
    LOG_SCOPE_F(INFO, "Initializing CubeGlobal");

    // Store the vertices on the client side arrays

    fo::resize(c.cube_points, 8);
    std::copy(CUBE_POINTS, CUBE_POINTS + 8, fo::begin(c.cube_points));

    fo::resize(c.cube_normals, 8);
    for (u32 i = 0; i < 8; ++i) {
        c.cube_normals[i] = normalize(c.cube_points[i]);
    }

    fo::resize(c.cube_tcoords, 8);
    std::copy(ARRAY_BEGIN(CUBE_TCOORDS), ARRAY_BEGIN(CUBE_TCOORDS), fo::begin(c.cube_tcoords));

    c.vbo_bytes = vec_bytes(c.cube_points) + vec_bytes(c.cube_normals) + vec_bytes(c.cube_tcoords);
    c.normals_start_offset = vec_bytes(c.cube_points);
    c.tcoords_start_offset = vec_bytes(c.cube_normals);

    // Indices
    fo::resize(c.cube_indices, 6 * 6);
    std::copy(reinterpret_cast<const u16 *>(ARRAY_BEGIN(CUBE_INDICES)),
              reinterpret_cast<const u16 *>(ARRAY_END(CUBE_INDICES)),
              fo::begin(c.cube_indices));

    c.num_indices = 6 * 3;

    // Generate each buffer.

    // Allocate a big enough vertex buffer to hold pos, normal, tcoord
    glCreateBuffers(1, &c.vbo);
    glNamedBufferData(c.vbo, c.vbo_bytes, nullptr, GL_STATIC_DRAW);

    // Fill the buffer with the vertex data
    glNamedBufferSubData(c.vbo, vec_bytes(c.cube_points), c.pos_start_offset, fo::data(c.cube_points));
    glNamedBufferSubData(c.vbo, vec_bytes(c.cube_normals), c.normals_start_offset, fo::data(c.cube_normals));
    glNamedBufferSubData(c.vbo, vec_bytes(c.cube_tcoords), c.tcoords_start_offset, fo::data(c.cube_tcoords));

    // Generate the index buffer
    glCreateBuffers(1, &c.ebo);
    glNamedBufferData(c.ebo, vec_bytes(c.cube_indices), fo::data(c.cube_indices), GL_STATIC_DRAW);
}

void bind_vertex_buffer(CubeGlobal &c) {
    glBindVertexBuffer(0, c.vbo, c.pos_start_offset, sizeof(Vector3));
    glBindVertexBuffer(0, c.vbo, c.normals_start_offset, sizeof(Vector3));
    glBindVertexBuffer(0, c.vbo, c.tcoords_start_offset, sizeof(Vector2));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ebo);
}
