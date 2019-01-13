// Header for chores
#pragma once

#include <glad/glad.h>

#include <learnogl/stb_image.h>
#include <scaffold/memory.h>
#include <stdlib.h>
#include <string.h>

// Flips the image about the x-axis to make it compliant with ogl's
// interpretation. This functionality is also provided by stb_image itself.
void flip_vertically(unsigned char *image_data, int width, int height) {
    const int row_size = width * 4; // row size in bytes
    auto tmp_buf = (unsigned char *)fo::memory_globals::default_scratch_allocator().allocate(row_size);

    for (int row = 0; row < height / 2; ++row) {
        int opposite_row = height - 1 - row;
        memcpy(tmp_buf, &image_data[row * row_size], row_size);
        memcpy(&image_data[row * row_size], &image_data[opposite_row * row_size], row_size);
        memcpy(&image_data[opposite_row * row_size], tmp_buf, row_size);
    }

    fo::memory_globals::default_scratch_allocator().deallocate(tmp_buf);
}

constexpr bool normal_printing_test = false;
constexpr bool use_stb_image_flip = false;

// Loads a texture using stb_image. Returns true if succeeded, otherwise
// false.
bool load_texture(const char *file_name, GLuint *tex, GLenum texture_slot = GL_TEXTURE0) {
    int x, y, n;
    int force_channels = 4;

    if (use_stb_image_flip) {
        stbi_set_flip_vertically_on_load(1);
    }

    unsigned char *image_data = stbi_load(file_name, &x, &y, &n, force_channels);
    if (!image_data) {
        fprintf(stderr, "ERROR: could not load %s\n", file_name);
        return false;
    }
    // NPOT check
    if ((x & (x - 1)) != 0 || (y & (y - 1)) != 0) {
        fprintf(stderr, "WARNING: texture %s is not power-of-2 dimensions\n", file_name);
    }

    if (!use_stb_image_flip) {
        flip_vertically(image_data, x, y);
    }

    if (normal_printing_test) {
        for (uint8_t *block = image_data, *end = image_data + (x * y); block < end; block += 4) {
            float x = (2 * float(block[0]) - 1.0) / 255.0;
            float y = (2 * float(block[1]) - 1.0) / 255.0;
            float z = (2 * float(block[2]) - 1.0) / 255.0;
            float w = (2 * float(block[3]) - 1.0) / 255.0;
            printf("Normal %li: [%f\t%f\t%f\t%f]\n", (block - image_data) / 4, x, y, z, w);
        }
        exit(EXIT_SUCCESS);
    }

    glGenTextures(1, tex);
    glActiveTexture(texture_slot);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // GLfloat max_aniso = 0.0f;
    // glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
    // glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso);

    stbi_image_free(image_data);
    return true;
}