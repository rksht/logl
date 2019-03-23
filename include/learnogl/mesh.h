#pragma once

#include <learnogl/kitchen_sink.h>
#include <scaffold/array.h>
#include <scaffold/math_types.h>

namespace eng {
namespace mesh {

using IndexType = u16;

constexpr u32 ATTRIBUTE_NOT_PRESENT = std::numeric_limits<u32>::max();
constexpr u32 MAX_BONES_AFFECTING_VERTEX = 5;
constexpr i32 INVALID_BONE_ID = -1;

// Given N = number of vertices, M = number of indices in mesh, the data is laid out like so:
//
// | pnut_0 | pnut_1 |...| pnut_{N-1} |
// | index_0 | index_1 |...| index_{M-1} |
// | afb_0 | afb_1 | ... | afb_{N-1} |
//
// "pnut" means position, normal, uv, tangent. The last 3 attributes are optional depending on the mesh.
struct MeshDataOffsetsAndSizes {
    u32 num_vertices;     // Number of (unique) vertices in the mesh
    u32 num_faces;        // Number of faces (triangles really) in the mesh
    u32 packed_attr_size; // The size of each attribute pack
    u32 position_offset;  // Offset of position attribute data in a pack
    u32 normal_offset;    // Offset of normal attribute data in a pack
    u32 tex2d_offset;     // Offset of 2d texcoord attribute data in a pack
    u32 tangent_offset;   // Offset of tangent attribute data in a pack
    u32 num_bones;        // Number of bones in the skinned mesh.
    u32 bone_data_offset; // Bone data offset(Not needed..? Place them after indices)

    u32 get_vertices_size_in_bytes() const { return num_vertices * packed_attr_size; }
    u32 get_indices_size_in_bytes() const { return num_faces * 3 * sizeof(u16); }

    // Returns the amount of bytes needed by the affecting bones list.
    inline u32 get_affecting_bones_size_in_bytes() const;

    // Returns the amount of bytes needed by the offset transform list.
    inline u32 get_offset_transform_size_in_bytes() const;

    // Sum of above two really.
    inline u32 get_bone_data_size_in_bytes() const;

    u32 get_vertices_byte_offset() const { return 0; }
    u32 get_indices_byte_offset() const { return get_vertices_size_in_bytes(); }
    u32 get_bones_byte_offset() const { return get_vertices_size_in_bytes() + get_indices_size_in_bytes(); }
    u32 get_affecting_bones_byte_offset() const { return get_bones_byte_offset(); }
    u32 get_offset_transforms_byte_offset() const {
        return get_bones_byte_offset() + get_affecting_bones_size_in_bytes();
    }
};

inline bool have_same_attributes(const MeshDataOffsetsAndSizes &mo1, const MeshDataOffsetsAndSizes &mo2) {
    return mo1.position_offset == mo2.position_offset && mo1.normal_offset == mo2.normal_offset &&
           mo1.tex2d_offset == mo2.tex2d_offset && mo1.tangent_offset == mo2.tangent_offset;
}

// Each bone has an unique id in the model. The id for a bone is just an index into arrays of data.

// List of affecting bones for a vertex. We don't store explicit count. If there's less than
// MAX_BONES_AFFECTING_VERTEX affecting the vertex, we store 0.0 as the weight of the remaining slots.
struct AffectingBones {
    i32 bone_ids[MAX_BONES_AFFECTING_VERTEX];
    f32 weights[MAX_BONES_AFFECTING_VERTEX];

    constexpr AffectingBones()
        : bone_ids{}
        , weights{} {}

    static constexpr AffectingBones get_empty() {
        AffectingBones a{};
        for (u32 i = 0; i < MAX_BONES_AFFECTING_VERTEX; ++i) {
            a.bone_ids[i] = -1;
        }
        return a;
    }

    u32 count() {
        u32 i = 0;
        while (i < MAX_BONES_AFFECTING_VERTEX) {
            if (bone_ids[i] == INVALID_BONE_ID) {
                break;
            }
        }
        return i;
    }
};

struct OffsetTransform {
    fo::Matrix4x4 m;
};

inline u32 MeshDataOffsetsAndSizes::get_affecting_bones_size_in_bytes() const {
    return SELF.num_bones * sizeof(AffectingBones);
}

inline u32 MeshDataOffsetsAndSizes::get_offset_transform_size_in_bytes() const {
    return SELF.num_bones * sizeof(OffsetTransform);
}

inline u32 MeshDataOffsetsAndSizes::get_bone_data_size_in_bytes() const {
    return SELF.num_bones * (sizeof(AffectingBones) + sizeof(OffsetTransform));
}

struct BonesDataPointers {
    // We keep the per-vertex lists of affecting bones.
    AffectingBones *affecting_bones;

    // An array of offset transform of each bone in order of bone index.
    fo::Matrix4x4 *offset_transforms;
};

// Represents the format of 3D triangle-list mesh. All sizes and offsets are in bytes. Can be sourced into vbo
// and ebo and should be drawn with GL_TRIANGLES.
struct MeshData {
    MeshDataOffsetsAndSizes o;
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
    u32 bone_data_offset;
    bool positions_are_2d;

    StrippedMeshData() = default;

    StrippedMeshData(const MeshDataOffsetsAndSizes &mdo, bool positions_are_2d = false) {
        num_vertices = mdo.num_vertices;
        num_faces = mdo.num_faces;
        packed_attr_size = mdo.packed_attr_size;
        position_offset = mdo.position_offset;
        normal_offset = mdo.normal_offset;
        tex2d_offset = mdo.tex2d_offset;
        tangent_offset = mdo.tangent_offset;
        bone_data_offset = mdo.bone_data_offset;

        this->positions_are_2d = positions_are_2d;
    }
};

inline StridedIterator<fo::Vector3, false> positions_begin(MeshData &m) {
    return StridedIterator<fo::Vector3, false>(m.buffer, m.o.packed_attr_size);
}

inline StridedIterator<fo::Vector3, false> positions_end(MeshData &m) {
    return StridedIterator<fo::Vector3, false>(m.buffer + m.o.packed_attr_size * m.o.num_vertices,
                                               m.o.packed_attr_size);
}

inline StridedIterator<fo::Vector3, true> positions_begin(const MeshData &m) {
    return StridedIterator<fo::Vector3, true>(m.buffer, m.o.packed_attr_size);
}

inline StridedIterator<fo::Vector3, true> positions_end(const MeshData &m) {
    return StridedIterator<fo::Vector3, true>(m.buffer + m.o.packed_attr_size * m.o.num_vertices,
                                              m.o.packed_attr_size);
}

inline const uint16_t *indices_begin(MeshData &m) {
    return reinterpret_cast<uint16_t *>(m.buffer + m.o.get_vertices_size_in_bytes());
}

inline const uint16_t *indices_end(MeshData &m) { return indices_begin(m) + m.o.num_faces * 3; }

constexpr u32 max_children_bones = 5;

struct SkeletonNode {
    std::array<SkeletonNode *, max_children_bones> children;
    std::array<char, 64> name; // This bone's name
    u32 num_children;
    u32 bone_index;
};

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
    IGNORE_BONES = 1 << 5,
};

// Loads the model specified in the given file into `m`, which must not be containing any model.
bool load(Model &m,
          const char *file_name,
          fo::Vector2 fill_uv = {},
          u32 model_load_flags = ModelLoadFlagBits::TRIANGULATE | ModelLoadFlagBits::CALC_NORMALS);

// Loads the model and transforms each position, normal and tangent (if present) with the given transform
// matrix. The linear part of the transform must be an orthogonal matrix therefore.
bool load_then_transform(Model &m,
                         const char *file_name,
                         fo::Vector2 fill_uv,
                         u32 model_load_flags,
                         const fo::Matrix4x4 &transform);

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
