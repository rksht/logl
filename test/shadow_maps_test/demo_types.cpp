#include "demo_types.h"

void load_dir_light_mesh(mesh::Model &m) {
    // eng::load_cube_mesh(m, identity_matrix, true, true);
    // return;

    // Number of faces = 3 * N + 3 * N. No normals and textures.
    push_back(m._mesh_array, {});
    auto &md = back(m._mesh_array);

    const u32 num_divs = 16;
    md.num_vertices = 1 + num_divs + 1; // Center, Circumferance points, Tip
    md.num_faces = 2 * num_divs;
    md.position_offset = 0;
    md.normal_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.tex2d_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    md.packed_attr_size = sizeof(Vector3);
    md.buffer = reinterpret_cast<u8 *>(
        m._buffer_allocator->allocate(sizeof(Vector3) * md.num_vertices + sizeof(u16) * md.num_faces * 3));

    Vector3 *pos = reinterpret_cast<Vector3 *>(md.buffer);

    f32 current_angle = 0.0f;

    pos[0].x = 0.0f;
    pos[0].y = 0.0f;
    pos[0].z = 0.0f;

    const f32 arc_angle = 2.0f * pi / (f32)num_divs;

    for (u32 i = 1; i <= num_divs; ++i) {
        pos[i].x = std::cos(current_angle);
        pos[i].y = std::sin(current_angle);
        pos[i].z = 1.0f;
        current_angle += arc_angle;
    }

    pos[num_divs + 1] = Vector3{0, 0, -1.0f};

    u16 *indices = reinterpret_cast<u16 *>(md.buffer + sizeof(Vector3) * md.num_vertices);

    for (u32 face = 0; face < num_divs; ++face) {
        indices[face * 3] = 0;
        indices[face * 3 + 1] = (face + 1) % num_divs;
        indices[face * 3 + 2] = (face + 2) % num_divs;
    }

    for (u32 face = num_divs, j = 1; face < num_divs * 2; ++face, ++j) {
        indices[face * 3] = num_divs + 1;
        indices[face * 3 + 1] = j;
        indices[face * 3 + 2] = (j + 1) % num_divs;
    }

#if 0
    for (u32 i = 0; i < 2 * num_divs * 3; i += 3) {
        LOG_F(INFO,
              "\n\tFace = [%u, %u, %u],\n\tVerts: [%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f]",
              indices[i],
              indices[i + 1],
              indices[i + 2],
              XYZ(pos[indices[i]]),
              XYZ(pos[indices[i + 1]]),
              XYZ(pos[indices[i + 2]]));
    }
#endif
}
