// Based on OpenGL Redbook accompanied code
#pragma once

#include <learnogl/gl_misc.h>
#include <scaffold/memory.h>

namespace dds {

// Enough mips for 16K x 16K, which is the minumum required for OpenGL 4.x
constexpr unsigned MAX_TEXTURE_MIPS = 14;

// Each texture image data structure contains an array of MAX_TEXTURE_MIPS
// of these mipmap structures. The structure represents the mipmap data for
// all slices at that level.
struct ImageMipData {
    fo::Allocator *allocator; // Used to allocate the data.

    GLsizei width;        // Width of this mipmap level
    GLsizei height;       // Height of this mipmap level
    GLsizei depth;        // Depth pof mipmap level
    GLsizeiptr mipStride; // Distance in bytes between mip levels in memory
    GLvoid *data;         // Pointer to data
};

// This is the main image data structure. It contains all the parameters needed
// to place texture data into a texture object using OpenGL.
struct ImageData {
    GLenum target;                      // Texture target (1D, 2D, cubemap, array, etc.)
    GLenum internalFormat;              // Recommended internal format (GL_RGBA32F, etc).
    GLenum format;                      // Format in memory
    GLenum type;                        // Type in memory (GL_RED, GL_RGB, etc.)
    GLenum swizzle[4];                  // Swizzle for RGBA
    GLsizei mipLevels;                  // Number of present mipmap levels
    GLsizei slices;                     // Number of slices (for arrays)
    GLsizeiptr sliceStride;             // Distance in bytes between slices of an array texture
    GLsizeiptr totalDataSize;           // Complete amount of data allocated for texture
    ImageMipData mip[MAX_TEXTURE_MIPS]; // Actual mipmap data

    fo::Allocator *allocator; // Used to allocate the data. nullptr denotes unloaded image.
};

bool load_image(const fs::path &file, ImageData *image_data, fo::Allocator *allocator);
void unload_image(ImageData *image_data);

/// Creates a texture object. `image` be nullptr if you don't want to use the
/// image information further after calling this function.
GLuint load_texture(const fs::path &file, GLuint texture, ImageData *image, fo::Allocator *allocator);

} // namespace dds
