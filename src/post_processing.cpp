#include <fmt/format.h>
#include <learnogl/post_processing.h>
#include <learnogl/shader.h>

// Common UBO shared by all blocks (of separately run compute shaders)
static constexpr u32 HDR_PARAM_UBO_BYTES = 16 * 1024;

struct ParamsUB {
    u32 param[4];
};

struct FBOCreateConfig {
    u32 width;
    u32 height;
    u32 msaa_samples = 1;
    bool with_depth_texture = true;
    GLenum depth_texture_format = GL_DEPTH_COMPONENT32F;
};

struct HDRFramebufferResources {
    eng::FBO _fbo;
    u32 width, height, msaa_samples;
    eng::TextureDeleter _color_texture;
    eng::TextureDeleter _depth_texture;

    // If HDR fbo is multisampled, this framebuffer will be the one that the samples get resolved to.
    eng::FBO _resolved_fbo;

    eng::TextureDeleter _color_texture_resolved;
    eng::TextureDeleter _depth_texture_resolved;

    eng::BufferDeleter _params_ubo;
    GLuint _params_ubo_bindpoint;

    // Two SSBOs for ping-ponging during the reduction to 1D
    eng::BufferDeleter _1d_ssbo_deleters[2];
    GLuint _1d_ssbo_bindpoints[2];
};

void create_hdr_framebuffer(const FBOCreateConfig &c,
                            HDRFramebufferResources &hdr_fb,
                            eng::BindingState &bs) {
    CHECK_F(c.depth_texture_format == GL_DEPTH_COMPONENT32F, "Not supporting other depth formats for a now");
    CHECK_F(c.msaa_samples != 0);
    hdr_fb.width = c.width;
    hdr_fb.height = c.height;
    hdr_fb.msaa_samples = c.msaa_samples;

    const GLenum texture_type = hdr_fb.msaa_samples == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE;

    GLuint texture = 0;
    GLuint depth_texture = 0;

    glCreateTextures(texture_type, 1, &texture);
    if (c.with_depth_texture) {
        glCreateTextures(texture_type, 1, &depth_texture);
    }

    if (texture_type == GL_TEXTURE_2D) {
        // Color
        glTextureStorage2D(texture, 1, GL_RGBA16F, hdr_fb.width, hdr_fb.height);
        // Depth
        if (c.with_depth_texture) {
            glTextureStorage2D(depth_texture, 1, c.depth_texture_format, hdr_fb.width, hdr_fb.height);
        }
    } else {
        // Color
        glTextureStorage2DMultisample(
            texture, hdr_fb.msaa_samples, GL_RGBA16F, hdr_fb.width, hdr_fb.height, GL_TRUE);
        // Depth
        if (c.with_depth_texture) {
            glTextureStorage2DMultisample(
                texture, hdr_fb.msaa_samples, c.depth_texture_format, hdr_fb.width, hdr_fb.height, GL_TRUE);
        }
    }

    hdr_fb._color_texture.set(texture);
    if (c.with_depth_texture) {
        hdr_fb._depth_texture.set(depth_texture);
    }

    // Generate the fbo
    hdr_fb._fbo.gen().bind().add_attachment(0, hdr_fb._color_texture.handle());
    if (c.with_depth_texture) {
        hdr_fb._fbo.add_depth_attachment(hdr_fb._depth_texture.handle());
    }
    hdr_fb._fbo.set_done_creating().bind_as_readable();

    if (c.msaa_samples > 1) {
        GLuint resolved_color_texture = 0, resolved_depth_texture = 0;
        glCreateTextures(GL_TEXTURE_2D, 1, &resolved_color_texture);

        if (c.with_depth_texture) {
            glCreateTextures(GL_TEXTURE_2D, 1, &resolved_depth_texture);
        }

        glTextureStorage2D(resolved_color_texture, 1, GL_RGBA16F, hdr_fb.width, hdr_fb.height);

        // Depth
        if (c.with_depth_texture) {
            glTextureStorage2D(
                resolved_depth_texture, 1, c.depth_texture_format, hdr_fb.width, hdr_fb.height);
        }

        hdr_fb._resolved_fbo.gen().bind().add_attachment(0, resolved_color_texture);

        if (c.with_depth_texture) {
            hdr_fb._resolved_fbo.add_depth_attachment(resolved_depth_texture);
        }

        hdr_fb._color_texture_resolved.set(resolved_color_texture);
        hdr_fb._depth_texture_resolved.set(resolved_depth_texture);
        hdr_fb._fbo.set_draw_buffers({ 0 });
        hdr_fb._fbo.set_done_creating();
    }

    // Allocate a binding point for the params ubo
    {
        GLuint params_ubo;
        glGenBuffers(1, &params_ubo);
        eng::gl_desc::UniformBuffer ubo_desc(params_ubo, 0, HDR_PARAM_UBO_BYTES);
        hdr_fb._params_ubo_bindpoint = bs.bind_unique(ubo_desc);
        hdr_fb._params_ubo.set(params_ubo);

        eng::set_buffer_label(params_ubo, "@postproc_params_ubo");
    }

    // Allocate two binding points for the reduced result ssbo
    for (int i = 0; i <= 1; ++i) {
        GLuint oned_ssbo;
        glGenBuffers(1, &oned_ssbo);

        u32 bytes = ceil_div(hdr_fb.width, 8) * ceil_div(hdr_fb.height, 8) * sizeof(float);
        eng::gl_desc::ShaderStorageBuffer ssbo_desc(oned_ssbo, 0, bytes);
        hdr_fb._1d_ssbo_bindpoints[0] = bs.bind_unique(ssbo_desc);
        hdr_fb._1d_ssbo_deleters[0].set(oned_ssbo);
        eng::set_buffer_label(oned_ssbo, fmt::format("@reduce_1d_ssbo[{}]", i).c_str());
    }
}

// void reduce_to_1d_ssbo(HDRFramebufferResources &hdr_fb, ReductionResultResources &reduced_result);

void measure_average_luminance(HDRFramebufferResources &hdr_fb) {
    int dimx = ceil_div(hdr_fb.width, 8);
    dimx = ceil_div(dimx, 2);

    int dimy = ceil_div(hdr_fb.height, 8);
    dimy = ceil_div(dimy, 2);

    // Reduce the fbo color texture to a 1D buffer

    ParamsUB params_ub = { 0, 0, hdr_fb.width, hdr_fb.height };

    glInvalidateBufferData(hdr_fb._params_ubo.handle());
    glNamedBufferSubData(hdr_fb._params_ubo.handle(), 0, sizeof(ParamsUB), &params_ub);
}

// Call after rendering the scene to hdr_fb._fbo.
void do_hdr(HDRFramebufferResources &hdr_fb) {
    eng::FBO *fbo_to_run_on = nullptr;

    // Resolve the framebuffer if it's MSAA enabled
    if (hdr_fb.msaa_samples > 1) {

        GLbitfield blit_mask = GL_COLOR_BUFFER_BIT;

        if (hdr_fb._depth_texture.is_created()) {
            blit_mask = GL_DEPTH_BUFFER_BIT;
        }

        hdr_fb._fbo.bind_as_readable(GLuint(hdr_fb._resolved_fbo));

        // clang-format off
        glBlitNamedFramebuffer(GLuint(hdr_fb._fbo), GLuint(hdr_fb._resolved_fbo),
                               0, 0, hdr_fb.width, hdr_fb.height,
                               0, 0, hdr_fb.width, hdr_fb.height,
                               blit_mask,
                               GL_LINEAR);
        // clang-format on
        fbo_to_run_on = &hdr_fb._resolved_fbo;
    } else {
        fbo_to_run_on = &hdr_fb._fbo;
    }

    // Set the framebuffer to run the average on as the read framebuffer.
    fbo_to_run_on->bind_as_readable(0);

    measure_average_luminance(hdr_fb);
}
