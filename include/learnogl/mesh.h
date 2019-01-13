#pragma once

#include <learnogl/kitchen_sink.h>
#include <scaffold/array.h>
#include <scaffold/math_types.h>

namespace eng {
namespace mesh {

using IndexType = u16;

constexpr u32 ATTRIBUTE_NOT_PRESENT = std::numeric_limits<u32>::max();

// Represents the format of 3D triangle-list mesh. All sizes and offsets are in bytes. Can be sourced into vbo
// and ebo and should be drawn with GL_TRIANGLES.
struct MeshData {
    u32 num_vertices;      // Number of (unique) vertices in the mesh
    u32 num_faces;         // Number of faces in the mesh
    u32 packed_attr_size;  // The size of each attribute pack
    u32 position_offset;   // Offset of position attribute data in a pack
    u32 normal_offset;     // Offset of normal attribute data in a pack
    u32 tex2d_offset;      // Offset of 2d texcoord attribute data in a pack
    u32 tangent_offset;    // Offset of tangent attribute data in a pack
    u8 *buffer;            // The array of attribute packs
    bool positions_are_2d; // TODO: pack it into `num_vertices` perhaps.
};

// Only the info for the mesh. Doesn't hold a pointer to the buffer of vertex attributes.
struct StrippedMeshData {
    u32 num_vertices;
    u32 num_faces;
    u32 packed_attr_size;
    u32 position_offset;
    u32 normal_offset;
    u32 tex2d_offset;
    u32 tangent_offset;
    bool positions_are_2d;

    StrippedMeshData() = default;

    StrippedMeshData(const MeshData &md) {
        num_vertices = md.num_vertices;
        num_faces = md.num_faces;
        packed_attr_size = md.packed_attr_size;
        position_offset = md.position_offset;
        normal_offset = md.normal_offset;
        tex2d_offset = md.tex2d_offset;
        tangent_offset = md.tangent_offset;
        positions_are_2d = md.positions_are_2d;
    }
};

// -- Functions on `MeshData`. Just getters here.

#define MESH_DATA_ONLY static_assert(one_of_type_v<M, MeshData, StrippedMeshData>, "")

template <typename M> inline u32 num_vertices(const M &m) {
    MESH_DATA_ONLY;
    return m.num_vertices;
}

template <typename M> inline u32 num_indices(const M &m) {
    MESH_DATA_ONLY;
    return m.num_faces * 3;
}

template <typename M> inline u32 packed_attr_size(const M &m) {
    MESH_DATA_ONLY;
    return m.packed_attr_size;
}

template <typename M> inline u32 vertex_buffer_size(const M &m) {
    MESH_DATA_ONLY;
    return m.num_vertices * m.packed_attr_size;
}

template <typename M> inline u32 indices_offset(const M &m) {
    MESH_DATA_ONLY;
    return vertex_buffer_size(m);
}

template <typename M> inline u32 index_buffer_size(const M &m) {
    MESH_DATA_ONLY;
    return m.num_faces * 3 * sizeof(IndexType);
}

template <typename M> inline u32 total_buffer_size(const M &m) {
    MESH_DATA_ONLY;
    return vertex_buffer_size(m) + index_buffer_size(m);
}

template <typename M> inline const IndexType *indices(const M &m) {
    MESH_DATA_ONLY;
    return reinterpret_cast<const IndexType *>(m.buffer + indices_offset(m));
}

template <typename M> inline IndexType *indices(M &m) {
    MESH_DATA_ONLY;
    return reinterpret_cast<IndexType *>(m.buffer + indices_offset(m));
}

template <typename M> inline const uint8_t *vertices(const M &m) {
    MESH_DATA_ONLY;
    return m.buffer;
}

template <typename M> inline uint8_t *vertices(M &m) {
    MESH_DATA_ONLY;
    return m.buffer;
}

template <typename M> inline bool have_same_attributes(const M &m1, const M &m2) {
    return m1.position_offset == m2.position_offset && m1.normal_offset == m2.normal_offset &&
           m1.tex2d_offset == m2.tex2d_offset && m1.tangent_offset == m2.tangent_offset;
}

#undef MESH_DATA_ONLY

inline StridedIterator<fo::Vector3, false> positions_begin(MeshData &m) {
    return StridedIterator<fo::Vector3, false>(vertices(m), m.packed_attr_size);
}

inline StridedIterator<fo::Vector3, false> positions_end(MeshData &m) {
    return StridedIterator<fo::Vector3, false>(vertices(m) + m.packed_attr_size * num_vertices(m),
                                               m.packed_attr_size);
}

inline StridedIterator<fo::Vector3, true> positions_begin(const MeshData &m) {
    return StridedIterator<fo::Vector3, true>(vertices(m), m.packed_attr_size);
}

inline StridedIterator<fo::Vector3, true> positions_end(const MeshData &m) {
    return StridedIterator<fo::Vector3, true>(vertices(m) + m.packed_attr_size * num_vertices(m),
                                              m.packed_attr_size);
}

inline const uint16_t *indices_begin(MeshData &m) { return indices(m); }

inline const uint16_t *indices_end(MeshData &m) { return indices(m) + num_indices(m); }

/// Represents a single model which can contain multiple meshes
struct Model {
    // List of all meshes in the model
    fo::Array<MeshData> _mesh_array{ fo::memory_globals::default_allocator() };

    // Allocator used to allocate the MeshData for each model.
    fo::Allocator *_buffer_allocator = &fo::memory_globals::default_allocator();

    Model() = default;

    Model(fo::Allocator &mesh_info_allocator, fo::Allocator &mesh_buffer_allocator);

    // Don't want to copy these objects around
    Model(const Model &other) = delete;

    // Frees all mesh buffers if not already deallocated
    ~Model();

    // Returns reference to the i-th MeshData
    MeshData &operator[](u32 i) { return _mesh_array[i]; };

    const MeshData &operator[](u32 i) const { return _mesh_array[i]; };
};

// -- Functions on `Model`

enum ModelLoadFlagBits : u32 {
    TRIANGULATE = 1 << 0,
    CALC_TANGENTS = 1 << 1,
    CALC_NORMALS = 1 << 2,
    GEN_UV_COORDS = 1 << 3,
    FILL_CONST_UV = 1 << 4, // If model does not have uv coordinates, you can set its vertices to a given uv
};

// Loads the model specified in the given file into `m`, which must not be containing any model.
bool load(Model &m,
          const char *file_name,
          fo::Vector2 fill_uv = {},
          u32 model_load_flags = ModelLoadFlagBits::TRIANGULATE | ModelLoadFlagBits::CALC_NORMALS);

// Loads the model and transforms each position, normal and tangent (if present) with the given transform
// matrix. The linear part of the transform must be an orthogonal matrix therefore.
bool load_then_transform(
    Model &m, const char *file_name, fo::Vector2 fill_uv, u32 model_load_flags, const fo::Matrix4x4 &transform);

// Frees all the mesh buffers of this model. Must not be free already.
void free_mesh_buffers(Model &m);

inline u32 num_meshes(const Model &m) { return fo::size(m._mesh_array); }

// Return ith mesh's data in the model
inline MeshData &mesh_data(Model &m, u32 i) { return m._mesh_array[i]; }
inline const MeshData &mesh_data(const Model &m, u32 i) { return m._mesh_array[i]; }

// -- Extra types and functions

// Format for vertex data for calculating the tangent which is stored in the `t_and_h` member
struct ForTangentSpaceCalc {
    fo::Vector3 position;
    fo::Vector3 normal;
    fo::Vector2 st;
    fo::Vector4 t_and_h; // Tangent and handedness (w component = 1.0 or -1.0)
};

static_assert(offsetof(ForTangentSpaceCalc, t_and_h) ==
                  offsetof(ForTangentSpaceCalc, st) + sizeof(fo::Vector2),
              "");

void calculate_tangents(ForTangentSpaceCalc *vertices, u32 num_vertices, IndexType *indices, u32 num_indices);

} // namespace mesh

} // namespace eng
