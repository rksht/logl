// C wrapper for model loading

#include <learnogl/mesh.h>
#include <scaffold/array.h>

#define DEFAULT_ALLOCATOR fo::memory_globals::default_allocator()

#ifdef _MSC_VER
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

EXPORT void *create_model() {
    return MAKE_NEW(DEFAULT_ALLOCATOR, mesh::Model, DEFAULT_ALLOCATOR, DEFAULT_ALLOCATOR);
}

EXPORT void delete_model(void *model) { MAKE_DELETE(DEFAULT_ALLOCATOR, Model, (mesh::Model *)model); }

EXPORT int load_model(void *model, const char *file, uint32_t model_load_flags) {
    return (int)mesh::load(*reinterpret_cast<mesh::Model *>(model), file, model_load_flags);
}

EXPORT uint32_t num_meshes_in_model(void *model) {
    auto *m = reinterpret_cast<mesh::Model *>(model);
    return fo::size(m->_mesh_array);
}

// Returns i-th mesh data
EXPORT void *get_mesh_data(void *model, uint32_t i) {
    return &reinterpret_cast<mesh::Model *>(model)->_mesh_array[i];
}

// Returns the vertex buffer of given mesh data
EXPORT uint8_t *get_buffer(void *mesh_data) { return reinterpret_cast<mesh::MeshData *>(mesh_data)->buffer; }

// Returns the index buffer of given mesh data
EXPORT unsigned short *get_indices(void *mesh_data) {
    mesh::MeshData *md = reinterpret_cast<mesh::MeshData *>(mesh_data);
    return (unsigned short *)(md->buffer + mesh::indices_offset(*md));
}

// Returns the i-th position. `xyz` is a pointer to 3 uninited floats. It's
// not convenient to do pointer manipulation from ctypes, so it's better to
// just do it here.
EXPORT void get_position(const void *mesh_data, uint32_t i, float *xyz) {
    const mesh::MeshData *md = reinterpret_cast<const mesh::MeshData *>(mesh_data);
    auto pack = reinterpret_cast<const float *>(md->buffer + i * mesh::packed_attr_size(*md));
    xyz[0] = pack[0];
    xyz[1] = pack[1];
    xyz[2] = pack[2];
}

// Similar to above, but for getting the i-th normal.
EXPORT void get_normal(const void *mesh_data, uint32_t i, float *xyz) {
    const mesh::MeshData *md = reinterpret_cast<const mesh::MeshData *>(mesh_data);
    auto pack =
        reinterpret_cast<const float *>(md->buffer + i * mesh::packed_attr_size(*md) + md->normal_offset);
    xyz[0] = pack[0];
    xyz[1] = pack[1];
    xyz[2] = pack[2];
}

// Similar to above, but for getting the i-th 2D texture coord. `xy` must
// point to 2 floats.
EXPORT void get_tex2d(const void *mesh_data, uint32_t i, float *xy) {
    const mesh::MeshData *md = reinterpret_cast<const mesh::MeshData *>(mesh_data);
    auto pack =
        reinterpret_cast<const float *>(md->buffer + i * mesh::packed_attr_size(*md) + md->tex2d_offset);
    xy[0] = pack[0];
    xy[1] = pack[1];
}

// Similar to above, but for getting the i-th tangent. `xyzw` must point to
// four uninited floats.
EXPORT void get_tangent(const void *mesh_data, uint32_t i, float *xyzw) {
    const mesh::MeshData *md = reinterpret_cast<const mesh::MeshData *>(mesh_data);
    auto pack =
        reinterpret_cast<const float *>(md->buffer + i * mesh::packed_attr_size(*md) + md->tangent_offset);
    xyzw[0] = pack[0];
    xyzw[1] = pack[1];
    xyzw[2] = pack[2];
    xyzw[3] = pack[3];
}

EXPORT unsigned long num_vertices(const void *mesh_data) {
    return mesh::num_vertices(*reinterpret_cast<const mesh::MeshData *>(mesh_data));
}

EXPORT unsigned long num_indices(const void *mesh_data) {
    return mesh::num_indices(*reinterpret_cast<const mesh::MeshData *>(mesh_data));
}

EXPORT unsigned long vertex_buffer_size(const void *mesh_data) {
    return mesh::vertex_buffer_size(*reinterpret_cast<const mesh::MeshData *>(mesh_data));
}

EXPORT unsigned long indices_offset(const void *mesh_data) { return vertex_buffer_size(mesh_data); }

EXPORT unsigned long index_buffer_size(const void *mesh_data) {
    return mesh::index_buffer_size(*reinterpret_cast<const mesh::MeshData *>(mesh_data));
}

EXPORT unsigned long total_buffer_size(const void *mesh_data) {
    return mesh::total_buffer_size(*reinterpret_cast<const mesh::MeshData *>(mesh_data));
}

EXPORT unsigned long packed_attr_size(const void *mesh_data) {
    return mesh::packed_attr_size(*reinterpret_cast<const mesh::MeshData *>(mesh_data));
}

EXPORT void init_memory_globals() { fo::memory_globals::init(); }

EXPORT void shutdown_memory_globals() { fo::memory_globals::shutdown(); }

} // extern "C"
