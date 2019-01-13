#pragma once

#include "essentials.h"

#include <learnogl/stb_image.h>

GLuint load_texture(const char *file_name, GLenum texture_slot = GL_TEXTURE0) {
    int x, y, n;
    int force_channels = 4;

    stbi_set_flip_vertically_on_load(1);

    unsigned char *image_data = stbi_load(file_name, &x, &y, &n, force_channels);
    log_assert(image_data, "ERROR: could not load %s\n", file_name);
    // NPOT check
    if ((x & (x - 1)) != 0 || (y & (y - 1)) != 0) {
        log_warn("texture %s is not power-of-2 dimensions\n", file_name);
    }

    GLuint tex;

    glGenTextures(1, &tex);
    glActiveTexture(texture_slot);
    glBindTexture(GL_TEXTURE_2D, tex);
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

    return tex;
}
