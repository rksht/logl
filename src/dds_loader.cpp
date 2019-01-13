#include <learnogl/dds_loader.h>

#include <cstdint>

namespace dds {

bool load_dds(const char *filename, dds::ImageData *image, fo::Allocator *allocator);

bool load_image(const fs::path &file, ImageData *image, fo::Allocator *allocator) {
    return load_dds(file.u8string().c_str(), image, allocator);
}

void unload_image(ImageData *image) {
    // delete[] reinterpret_cast<uint8_t *>(image->mip[0].data);
    image->allocator->deallocate(image->mip[0].data);
    image->allocator = nullptr; // Invalid marker
}

GLuint load_texture(const fs::path &file, GLuint texture, ImageData *image, fo::Allocator *allocator) {
	ImageData local_image{};
    int level;

	if (!image) {
		image = &local_image;
		local_image.allocator = allocator;
	}

    bool succ = load_image(file, image, allocator);
	CHECK_F(succ, "Failed to load dds texture");

    if (texture == 0) {
        glGenTextures(1, &texture);
    }

    glBindTexture(image->target, texture);

    GLubyte *ptr = (GLubyte *)image->mip[0].data;

    switch (image->target) {
    case GL_TEXTURE_1D:
        glTexStorage1D(image->target, image->mipLevels, image->internalFormat, image->mip[0].width);
        for (level = 0; level < image->mipLevels; ++level) {
            glTexSubImage1D(GL_TEXTURE_1D, level, 0, image->mip[level].width, image->format, image->type,
                            image->mip[level].data);
        }
        break;
    case GL_TEXTURE_1D_ARRAY:
        glTexStorage2D(image->target, image->mipLevels, image->internalFormat, image->mip[0].width,
                       image->slices);
        for (level = 0; level < image->mipLevels; ++level) {
            glTexSubImage2D(GL_TEXTURE_1D, level, 0, 0, image->mip[level].width, image->slices, image->format,
                            image->type, image->mip[level].data);
        }
        break;
    case GL_TEXTURE_2D:
        glTexStorage2D(image->target, image->mipLevels, image->internalFormat, image->mip[0].width,
                       image->mip[0].height);
        for (level = 0; level < image->mipLevels; ++level) {
            glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, image->mip[level].width, image->mip[level].height,
                            image->format, image->type, image->mip[level].data);
        }
        break;
    case GL_TEXTURE_CUBE_MAP:
        for (level = 0; level < image->mipLevels; ++level) {
            ptr = (GLubyte *)image->mip[level].data;
            for (int face = 0; face < 6; face++) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, image->internalFormat,
                             image->mip[level].width, image->mip[level].height, 0, image->format, image->type,
                             ptr + image->sliceStride * face);
            }
        }
        break;
    case GL_TEXTURE_2D_ARRAY:
        glTexStorage3D(image->target, image->mipLevels, image->internalFormat, image->mip[0].width,
                       image->mip[0].height, image->slices);
        for (level = 0; level < image->mipLevels; ++level) {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, image->mip[level].width,
                            image->mip[level].height, image->slices, image->format, image->type,
                            image->mip[level].data);
        }
        break;
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        glTexStorage3D(image->target, image->mipLevels, image->internalFormat, image->mip[0].width,
                       image->mip[0].height, image->slices);
        break;
    case GL_TEXTURE_3D:
        glTexStorage3D(image->target, image->mipLevels, image->internalFormat, image->mip[0].width,
                       image->mip[0].height, image->mip[0].depth);
        for (level = 0; level < image->mipLevels; ++level) {
            glTexSubImage3D(GL_TEXTURE_3D, level, 0, 0, 0, image->mip[level].width, image->mip[level].height,
                            image->mip[level].depth, image->format, image->type, image->mip[level].data);
        }
        break;
    default:
        break;
    }

    glTexParameteriv(image->target, GL_TEXTURE_SWIZZLE_RGBA, reinterpret_cast<const GLint *>(image->swizzle));

    if (image == &local_image) {
        unload_image(image);
    }

    return texture;
}

} // namespace dds
