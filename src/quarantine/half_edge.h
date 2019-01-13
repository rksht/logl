#if 0

#pragma once

#include <scaffold/array.h>

namespace halfedge {

// The HalfEdge is templated over VertexType since it's usually a point of customizability.
template <typename VertexType> struct HalfEdge {
    VertexType vertex;
    u32 twin; // Pointer (index into array) of twin edge
    u32 next; // Pointer (index into array) of next edge surrounding the left side face
};

template <typename VertexType> struct Mesh {
    using HalfEdgeType = HalfEdge<VertexType>;
    fo::Array<HalfEdgeType> edge_store;

    Mesh(fo::Allocator &allocator)
        : edge_store(allocator) {}
};

// Create a half-edge-mesh from given triangle soup
bool create_from_triangle_soup(Mesh<u16> &mesh, u16 *indices, u32 num_indices);

} // namespace halfedge

// --- Impl

namespace halfedge {

using target_vert_mask = IntMask<0, 16, u32>;
using source_vert_mask = IntMask<16, 16, u32>;

bool create_from_triangle_soup(Mesh<u16> &mesh, u16 *indices, u32 num_indices) {
    const u32 num_faces = num_indices * 3;
    assert(size(mesh.edge_store) == 0);

    resize(mesh.edge_store, num_indices)

    // (source, target) -> pointer to edge
    auto src_target_edge = fo::make_pod_hash<u32, u32>(fo::memory_globals::default_allocator());

    for (u32 i = 0; i < num_indices; i += 3) {
        u16 v0 = indices[i];
        u16 v1 = indices[i + 1];
        u16 v2 = indices[i + 2];
    }
}

} // namespace halfedge
#endif