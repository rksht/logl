#pragma once

#include <glad/glad.h>

#include <assert.h>
#include <learnogl/app_loop.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/eye.h>
#include <learnogl/eng>
#include <learnogl/math_ops.h>
#include <learnogl/mesh.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/nf_simple.h>
#include <learnogl/rng.h>
#include <learnogl/stb_image.h>
#include <scaffold/array.h>
#include <scaffold/math_types.h>
#include <scaffold/temp_allocator.h>

struct TextureFormat {
    GLint internal_format;
    GLenum external_format_components;
    GLenum external_format_component_type;
};

inline GLuint load_texture(const char *file_name, TextureFormat texture_format, GLenum texture_slot) {
    int x, y, n;
    int force_channels = 4;

    stbi_set_flip_vertically_on_load(1);

    unsigned char *image_data = stbi_load(file_name, &x, &y, &n, force_channels);
    log_assert(image_data, "ERROR: could not load %s\n", file_name);
    // NPOT check
    if ((x & (x - 1)) != 0 || (y & (y - 1)) != 0) {
        log_warn("texture %s does not have power-of-2 dimensions\n", file_name);
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
    // GLfloat max_aniso = 0.0f;
    // glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
    // glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso);

    stbi_image_free(image_data);

    return tex;
}

inline GLuint create_vs_fs_prog(const char *vs_file, const char *fs_file) {
    using namespace fo;
    Array<uint8_t> vs_src(memory_globals::default_allocator());
    Array<uint8_t> fs_src(memory_globals::default_allocator());
    read_file(fs::path(vs_file), vs_src);
    read_file(fs::path(fs_file), fs_src);
    return eng::create_program((char *)data(vs_src), (char *)data(fs_src));
}

inline void flip_2d_array_vertically(uint8_t *data, uint32_t element_size, uint32_t w, uint32_t h) {
    assert(w > 0);
    assert(h > 0);

    uint32_t row_size = element_size * w;
    fo::Array<uint8_t> tmp(fo::memory_globals::default_allocator(), row_size);
    auto p = fo::data(tmp);

    for (uint32_t r = 0; r < h / 2; ++r) {
        uint32_t opposite = h - r - 1;
        uint8_t *r1 = data + row_size * r;
        uint8_t *r2 = data + row_size * opposite;
        memcpy(p, r1, row_size);
        memcpy(r1, r2, row_size);
        memcpy(r2, p, row_size);
    }
}
