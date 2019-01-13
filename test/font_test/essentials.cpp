#include "essentials.h"

#include <numeric>

using namespace fo;
using namespace math;

GLuint load_texture(const char *file_name, TextureFormat texture_format, GLenum texture_slot) {
    int x, y, n;
    int force_channels = 4;

    stbi_set_flip_vertically_on_load(1);

    unsigned char *image_data = stbi_load(file_name, &x, &y, &n, force_channels);
    log_assert(image_data, "ERROR: could not load %s\n", file_name);
    // NPOT check
    if ((x & (x - 1)) != 0 || (y & (y - 1)) != 0) {
        LOG_F(INFO, "Texture %s does not have power-of-2 sides\n", file_name);
    }

    GLuint tex;

    glGenTextures(1, &tex);
    glActiveTexture(texture_slot);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, texture_format.internal_format, x, y, 0,
                 texture_format.external_format_components, texture_format.external_format_component_type,
                 image_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    stbi_image_free(image_data);

    return tex;
}

Quad::Quad(Vector2 min, Vector2 max, float z, const Vector2 &bl, const Vector2 &tl, const Vector2 &tr,
           const Vector2 &br) {
    const Vector2 topleft = {min.x, max.y};
    const Vector2 bottomright = {max.x, min.y};

    auto set = [z](Vector3 &v, const Vector2 &corner) {
        v.x = corner.x;
        v.y = corner.y;
        v.z = z;
    };

    set(vertices[0].position, min);
    set(vertices[1].position, max);
    set(vertices[2].position, topleft);

    set(vertices[3].position, min);
    set(vertices[4].position, bottomright);
    set(vertices[5].position, max);

    vertices[0].st = bl;
    vertices[1].st = tr;
    vertices[2].st = tl;

    vertices[3].st = bl;
    vertices[4].st = br;
    vertices[5].st = tr;
}

void Quad::make_vao(GLuint *vbo, GLuint *vao, Quad::BindingStates bs) {
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);

    glBindVertexArray(*vao);

    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(vertices), vertices.data(), GL_STATIC_DRAW);

    // Be wary of this
    if (bs.pos_attrib_vbinding == bs.st_attrib_vbinding) {
        glBindVertexBuffer(0, *vbo, 0, sizeof(VertexData));
    }

    glVertexAttribBinding(bs.pos_attrib_location, bs.pos_attrib_vbinding); // pos
    glVertexAttribBinding(bs.st_attrib_location, bs.st_attrib_vbinding);   // st
    glVertexAttribFormat(bs.pos_attrib_location, 2, GL_FLOAT, GL_FALSE, offsetof(VertexData, position));
    glVertexAttribFormat(bs.st_attrib_location, 2, GL_FLOAT, GL_FALSE, offsetof(VertexData, st));
    glEnableVertexAttribArray(bs.pos_attrib_location);
    glEnableVertexAttribArray(bs.st_attrib_location);
}

/*
void generate_quad_vertices(std::vector<Vector2> &vertices, const Vector2 &min, const Vector2 &max) {
    vertices.resize(6);

    const Vector2 topleft = {min.x, max.y};
    const Vector2 bottomright = {max.x, min.y};

    vertices[0] = min;
    vertices[1] = max;
    vertices[2] = topleft;
    vertices[3] = min;
    vertices[4] = bottomright;
    vertices[5] = max;
}
*/

void generate_quad_vertices(std::vector<Vector3> &vertices, const Vector3 &min, const Vector3 &max) {
    vertices.resize(4);

    assert(min.z == max.z);

    const Vector3 topleft = {min.x, max.y, min.z};
    const Vector3 bottomright = {max.x, min.y, min.z};

    vertices[0] = min;
    vertices[1] = max;
    vertices[2] = topleft;
    vertices[3] = bottomright;
}

void generate_quad_indices(std::vector<uint16_t> &indices) {
    indices.resize(6);
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 0;
    indices[4] = 3;
    indices[5] = 1;
}

void generate_quad_texcoords(std::vector<Vector2> &texcoords, const Vector2 &bl, const Vector2 &tl,
                             const Vector2 &tr, const Vector2 &br) {
    texcoords.resize(4);
    texcoords[0] = bl;
    texcoords[1] = tr;
    texcoords[2] = tl;
    texcoords[3] = br;
}

void create_vbo_from_par_mesh(const par_shapes_mesh *mesh, GLuint *p_vbo, GLuint *p_ebo) {
    GLuint vbo, ebo;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    size_t total_size = mesh->npoints * sizeof(f32) * (3 + 3 + 2); // pos, normal, uv

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, total_size, nullptr, GL_STATIC_DRAW);

    glBufferSubData(GL_ARRAY_BUFFER, 0, mesh->npoints * 3 * sizeof(f32), mesh->points);

    glBufferSubData(GL_ARRAY_BUFFER, mesh->npoints * 3 * sizeof(f32), mesh->npoints * 3 * sizeof(f32),
                    mesh->normals);

    glBufferSubData(GL_ARRAY_BUFFER, mesh->npoints * 3 * sizeof(f32) * 2, mesh->npoints * 2 * sizeof(f32),
                    mesh->tcoords);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->ntriangles * 3 * sizeof(u16), mesh->triangles,
                 GL_STATIC_DRAW);

    *p_vbo = vbo;
    *p_ebo = ebo;
}
