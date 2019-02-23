#include <algorithm>
#include <assert.h>
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/math_ops.h>
#include <learnogl/mesh.h>
#include <scaffold/debug.h>

using namespace fo;

namespace eng {

static void init_mesh_buffer(
    const aiMesh *mesh, mesh::MeshData *info, Allocator *allocator, bool do_fill_uv, Vector2 fill_uv);

namespace mesh {

Model::Model(fo::Allocator &mesh_info_allocator, fo::Allocator &mesh_buffer_allocator)
    : _mesh_array(mesh_info_allocator)
    , _buffer_allocator(&mesh_buffer_allocator) {}

Model::~Model() {
    if (_buffer_allocator) {
        free_mesh_buffers(*this);
    }
}

bool load(Model &m, const char *file_name, Vector2 fill_uv, uint32_t model_load_flags) {
    unsigned postprocess_steps = 0;

    if (model_load_flags & ModelLoadFlagBits::TRIANGULATE) {
        postprocess_steps |= aiProcess_Triangulate;
    }

    if (model_load_flags & ModelLoadFlagBits::CALC_NORMALS) {
        postprocess_steps |= aiProcess_GenSmoothNormals;
    }

    if (model_load_flags & ModelLoadFlagBits::CALC_TANGENTS) {
        postprocess_steps |= aiProcess_CalcTangentSpace;
    }

    if (model_load_flags & ModelLoadFlagBits::GEN_UV_COORDS) {
        postprocess_steps |= aiProcess_GenUVCoords;
    }

    postprocess_steps |= aiProcess_JoinIdenticalVertices;

    const aiScene *assimp_scene = aiImportFile(file_name, postprocess_steps);

    if (assimp_scene == nullptr) {
        return false;
    }

    LOG_F(INFO,
          "Model file loaded: %s, meshes=%d, Animations=%d, Materials=%d, Textures=%d",
          file_name,
          assimp_scene->mNumMeshes,
          assimp_scene->mNumAnimations,
          assimp_scene->mNumMaterials,
          assimp_scene->mNumTextures);

    /*
    log_assert(assimp_scene->mNumMeshes <= MAX_MESHES_IN_MODEL, "Have %d meshes in scene, max %u allowed",
               assimp_scene->mNumMeshes, MAX_MESHES_IN_MODEL);
    */

    resize(m._mesh_array, assimp_scene->mNumMeshes);

    for (int i = 0; i < assimp_scene->mNumMeshes; ++i) {
        init_mesh_buffer(assimp_scene->mMeshes[i],
                         &m._mesh_array[i],
                         m._buffer_allocator,
                         model_load_flags & ModelLoadFlagBits::FILL_CONST_UV,
                         fill_uv);
    }

    aiReleaseImport(assimp_scene);

    return true;
}

void free_mesh_buffers(Model &m) {
    assert(m._buffer_allocator != nullptr && "Already freed the buffer?");
    for (MeshData &md : m._mesh_array) {
        m._buffer_allocator->deallocate(md.buffer);
        md.buffer = nullptr;
    }
    m._buffer_allocator = nullptr;
}

bool load_then_transform(
    Model &m, const char *file_name, Vector2 fill_uv, u32 model_load_flags, const Matrix4x4 &transform) {
    using namespace math;

    auto res = load(m, file_name, fill_uv, model_load_flags);
    if (!res) {
        return res;
    }

    for (auto &md : m._mesh_array) {
        for (u32 i = 0; i < md.o.num_vertices; ++i) {
            uint8_t *pack = md.buffer + i * md.o.packed_attr_size;
            Vector3 *position = (Vector3 *)(pack + md.o.position_offset);
            Vector3 *normal = (Vector3 *)(pack + md.o.normal_offset);
            Vector4 *tangent = (Vector4 *)(pack + md.o.tangent_offset);

            if (md.o.position_offset != ATTRIBUTE_NOT_PRESENT) {
                *position = transform * Vector4(*position, 1.0f);
            }

            if (md.o.normal_offset != ATTRIBUTE_NOT_PRESENT) {
                *normal = transform * Vector4(*normal, 0.0f);
            }

            if (md.o.tangent_offset != ATTRIBUTE_NOT_PRESENT) {
                Vector3 t = Vector3(*tangent);
                t = transform * Vector4(t, 0.0f);
                tangent->x = t.x;
                tangent->y = t.y;
                tangent->z = t.z;
            }
        }
    }

    return true;
}

} // namespace mesh

void init_mesh_buffer(
    const aiMesh *mesh, mesh::MeshData *info, Allocator *allocator, bool do_fill_uv, Vector2 fill_uv) {
    info->o.num_vertices = mesh->mNumVertices;
    info->o.num_faces = mesh->mNumFaces;
    info->o.position_offset = 0;
    info->o.normal_offset = 0;
    info->o.tex2d_offset = 0;
    info->o.tangent_offset = 0;

    CHECK_LT_F(
        info->o.num_vertices, std::numeric_limits<u16>::max(), "Mesh cannot be stored using 16 bit indices");

    // First we calculate the buffer size we need and set up the offsets of
    // each attribute array
    info->o.packed_attr_size = 0;

    // Don't actually need to check this, it's always true for assimp with the
    // flags we are using.
    if (mesh->HasPositions()) {
        info->o.packed_attr_size += sizeof(Vector3);
    } else {
        info->o.position_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    }

    if (mesh->HasNormals()) {
        info->o.normal_offset = info->o.packed_attr_size;
        info->o.packed_attr_size += sizeof(Vector3);
    } else {
        info->o.normal_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    }

    if (mesh->HasTextureCoords(0) || do_fill_uv) {
        info->o.tex2d_offset = info->o.packed_attr_size;
        info->o.packed_attr_size += sizeof(Vector2);
    } else {
        info->o.tex2d_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    }

    if (mesh->HasTangentsAndBitangents()) {
        // xyspoon: This alignment is only here as a safeguard in case we really want to load this Vector4
        // into sse register right from the mesh buffer.
        uint32_t start = info->o.packed_attr_size;
        int mod = start % alignof(Vector4);
        if (mod) {
            info->o.packed_attr_size += alignof(Vector4) - mod;
        }
        info->o.tangent_offset = info->o.packed_attr_size;
        info->o.packed_attr_size += sizeof(Vector4);
    } else {
        info->o.tangent_offset = mesh::ATTRIBUTE_NOT_PRESENT;
    }

    // Again, don't need to check this, it's always true.
    assert(mesh->HasFaces());

    // Now we copy from assimp into the client mesh's buffer
    const size_t buffer_size = info->o.get_vertices_size_in_bytes() + info->o.get_indices_size_in_bytes();
    const uint32_t alignment = std::max(alignof(mesh::ForTangentSpaceCalc), size_t(64));
    info->buffer = (unsigned char *)allocator->allocate(buffer_size, alignment);

    debug(R"(
        Mesh has %u vertices
        Mesh has %u faces
        Packed attributes size = %u bytes
    )",
          info->o.num_vertices,
          info->o.num_faces,
          info->o.packed_attr_size);

    if (mesh->HasPositions()) {
        debug("    Mesh verts have positions");
        Vector3 *position = (Vector3 *)info->buffer;
        for (unsigned i = 0; i < info->o.num_vertices; ++i) {
            const aiVector3D *v = &mesh->mVertices[i];
            position->x = v->x;
            position->y = v->y;
            position->z = v->z;
            position = (Vector3 *)((char *)position + info->o.packed_attr_size);
        }
    }

    if (mesh->HasNormals()) {
        debug("    Mesh verts have normals");
        Vector3 *normal = (Vector3 *)(info->buffer + info->o.normal_offset);
        for (unsigned i = 0; i < info->o.num_vertices; ++i) {
            const aiVector3D *v = &mesh->mNormals[i];
            normal->x = v->x;
            normal->y = v->y;
            normal->z = v->z;
            normal = (Vector3 *)((char *)normal + info->o.packed_attr_size);
        }
    }

    if (mesh->HasTextureCoords(0)) {
        debug("    Mesh verts have texture coordinates");
        Vector2 *tex2d = (Vector2 *)(info->buffer + info->o.tex2d_offset);
        for (unsigned i = 0; i < info->o.num_vertices; ++i) {
            const aiVector3D *v = &mesh->mTextureCoords[0][i];
            tex2d->x = v->x;
            tex2d->y = v->y;
            tex2d = (Vector2 *)((char *)tex2d + info->o.packed_attr_size);
        }
    } else if (do_fill_uv) {
        Vector2 *tex2d = (Vector2 *)(info->buffer + info->o.tex2d_offset);
        for (unsigned i = 0; i < info->o.num_vertices; ++i) {
            const aiVector3D *v = &mesh->mTextureCoords[0][i];
            *tex2d = fill_uv;
            tex2d = (Vector2 *)((char *)tex2d + info->o.packed_attr_size);
        }
    }

    if (mesh->HasTangentsAndBitangents()) {
        debug("    Mesh verts have tangents and bitangents");
        Vector4 *tangent_p = (Vector4 *)(info->buffer + info->o.tangent_offset);
        for (unsigned i = 0; i < info->o.num_vertices; ++i) {
            const aiVector3D *tangent = &mesh->mTangents[i];
            const aiVector3D *bitangent = &mesh->mBitangents[i];
            const aiVector3D *normal = &mesh->mNormals[i];

            using namespace math;

            // See Eric Lengyel's book for tangent space transformation. The tangent, bitangent and normal
            // forms a basis. But we want an orthogonal basis specifically. So we orthogonalize the tangent,
            // and the bitangent we use is the cross prod of normal and tangent. This is a reasonable basis to
            // define the tangent space in even if `t`, `b`, `n` are not orthogonal.
            Vector3 t{ tangent->x, tangent->y, tangent->z };
            Vector3 b{ bitangent->x, bitangent->y, bitangent->z };
            Vector3 n{ normal->x, normal->y, normal->z };
            // So, the tangent `td` we shall use is perpendicular to the normal. It's w component stores the
            // handedness. `cross(n, td)` gives `w * b`. w is -1.0 or 1.0 depending on whether the det of the
            // transpose([t b n]) matrix is negative or positive.
            Vector4 td{
                normalize(t - dot(n, t) * n),             // The orthogonalized tangent
                dot(cross(n, t), b) < 0.0f ? -1.0f : 1.0f // The w component
            };
            tangent_p->x = td.x;
            tangent_p->y = td.y;
            tangent_p->z = td.z;
            tangent_p->w = td.w;

            assert(td.w == -1.0f || td.w == 1.0f);

            tangent_p = (Vector4 *)((char *)tangent_p + info->o.packed_attr_size);
        }
    }

    if (mesh->HasFaces()) {
        debug("    Mesh verts have faces, duh");
        unsigned short *p = (unsigned short *)(info->buffer + info->o.get_indices_size_in_bytes());
        for (unsigned i = 0; i < info->o.num_faces; ++i, p += 3) {
            const aiFace *face = &mesh->mFaces[i];
            assert(face->mNumIndices == 3 && "Assimp mesh's face doesn't have 3 indices.");
            p[0] = (unsigned short)(face->mIndices[0]);
            p[1] = (unsigned short)(face->mIndices[1]);
            p[2] = (unsigned short)(face->mIndices[2]);
            assert(p[0] < (1u << 16) - 1u);
            assert(p[1] < (1u << 16) - 1u);
            assert(p[2] < (1u << 16) - 1u);
        }
    }

    info->positions_are_2d = false;
}

/// Calculates tangent for a single triangle
static inline void calculate_tangent(mesh::IndexType i0,
                                     mesh::IndexType i1,
                                     mesh::IndexType i2,
                                     mesh::ForTangentSpaceCalc *vertices,
                                     Vector3 *bitangent_buffer) {
    auto &v0 = vertices[i0];
    auto &v1 = vertices[i1];
    auto &v2 = vertices[i2];
    const Vector3 &p0 = v0.position;
    const Vector3 &p1 = v1.position;
    const Vector3 &p2 = v2.position;
    const Vector2 &w0 = v0.st;
    const Vector2 &w1 = v1.st;
    const Vector2 &w2 = v2.st;
    const float x0 = p1.x - p0.x;
    const float x1 = p2.x - p0.x;
    const float y0 = p1.y - p0.y;
    const float y1 = p2.y - p1.y;
    const float z0 = p1.z - p0.z;
    const float z1 = p2.z - p1.z;

    const float s0 = w1.x - w0.x;
    const float s1 = w2.x - w0.x;
    const float t0 = w1.y - w0.y;
    const float t1 = w2.y - w0.y;

    const float r = 1.0f / (s0 * t1 - s1 * t0);
    Vector4 sdir{ (t1 * x0 - t0 * x1) * r, (t1 * y0 - t0 * y1) * r, (t1 * z0 - t0 * z1) * r, 0.0f };
    Vector3 tdir{ (s0 * x1 - s1 * x0) * r, (s0 * y1 - s1 * y0) * r, (s0 * z1 - s1 * z0) * r };

    using namespace math;

    v0.t_and_h = v0.t_and_h + sdir;
    v1.t_and_h = v1.t_and_h + sdir;
    v2.t_and_h = v2.t_and_h + sdir;
    bitangent_buffer[i0] = bitangent_buffer[i0] + tdir;
    bitangent_buffer[i1] = bitangent_buffer[i1] + tdir;
    bitangent_buffer[i2] = bitangent_buffer[i2] + tdir;
}

namespace mesh {

void calculate_tangents(ForTangentSpaceCalc *vertices,
                        uint32_t num_vertices,
                        IndexType *indices,
                        uint32_t num_indices) {
    Vector3 *bitangent_buffer =
        (Vector3 *)memory_globals::default_allocator().allocate(num_vertices * sizeof(Vector3));

    std::fill(bitangent_buffer, bitangent_buffer + num_vertices, Vector3{ 0, 0, 0 });

    log_assert(num_indices % 3 == 0, "");

    for (uint32_t i = 0; i < num_indices; i += 3) {
        calculate_tangent(indices[i], indices[i + 1], indices[i + 2], vertices, bitangent_buffer);
    }

    using namespace math;

    // Orthogonalize and store handedness bit
    for (uint32_t i = 0; i < num_vertices; ++i) {
        const Vector3 n = vertices[i].normal;
        const Vector3 t = vertices[i].t_and_h;
        vertices[i].t_and_h = Vector4(normalize(t - dot(n, t) * n), 0.0f);
        vertices[i].t_and_h.w = dot(cross(n, t), bitangent_buffer[i]) < 0.0f ? -1.0f : 1.0f;
    }

    memory_globals::default_allocator().deallocate(bitangent_buffer);
}

} // namespace mesh

} // namespace eng
