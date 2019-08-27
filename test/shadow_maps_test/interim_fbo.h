#pragma once

#include <learnogl/eng>

#include <learnogl/glsl_inspect.h>
#include <learnogl/render_utils.h>
#include <learnogl/shader.h>

#include <numeric>

#include <iostream>

namespace eng
{

    struct FBOCreateConfig {
        u32 width;
        u32 height;
        u32 msaa_samples = 1;
        bool with_depth_texture = true;
        GLenum depth_texture_format = GL_DEPTH_COMPONENT32F;
    };

    // TODO: Replace with eng::NewFBO.
    // An RGBA16F color attachment is what we will use to render the final scene into before post-processing
    // on it.
    struct FP16ColorFBO {
        GETONLY(TextureDeleter, color_texture);
        GETONLY(TextureDeleter, depth_texture);

        GETONLY(FBO, fbo);
        GETONLY(uint, msaa_samples) = 0;
        GETONLY(uint, width) = 0;
        GETONLY(uint, height) = 0;

        void create(const FBOCreateConfig &c)
        {
            CHECK_F(c.depth_texture_format == GL_DEPTH_COMPONENT32F,
                    "Not supporting other depth formats for a now");
            CHECK_F(c.msaa_samples != 0);
            _width = c.width;
            _height = c.height;
            _msaa_samples = c.msaa_samples;

            if (_msaa_samples > 1) {
                LOG_F(INFO,
                      "InterimFBO using MSAA samples = %s",
                      _msaa_samples > 1 ? "No MSAA" : fmt::format("{}", _msaa_samples).c_str());
            }

            const GLenum texture_type = _msaa_samples == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_MULTISAMPLE;

            GLuint texture = 0;
            GLuint depth_texture = 0;

            glCreateTextures(texture_type, 1, &texture);
            if (c.with_depth_texture) {
                glCreateTextures(texture_type, 1, &depth_texture);
            }

            if (texture_type == GL_TEXTURE_2D) {
                // Color
                glTextureStorage2D(texture, 1, GL_RGBA16F, _width, _height);
                // Depth
                if (c.with_depth_texture) {
                    glTextureStorage2D(depth_texture, 1, c.depth_texture_format, _width, _height);
                }
            } else {
                // Color
                glTextureStorage2DMultisample(texture, _msaa_samples, GL_RGBA16F, _width, _height, GL_TRUE);
                // Depth
                if (c.with_depth_texture) {
                    glTextureStorage2DMultisample(
                      texture, _msaa_samples, c.depth_texture_format, _width, _height, GL_TRUE);
                }
            }

            _color_texture.set(texture);
            if (c.with_depth_texture) {
                _depth_texture.set(depth_texture);
            }

            // Generate the fbo
            _fbo.gen().bind().add_attachment(0, _color_texture.handle());
            if (c.with_depth_texture) {
                _fbo.add_depth_attachment(_depth_texture.handle());
            }
            _fbo.set_done_creating().bind_as_readable();
        }

        bool is_msaa_texture() const { return _msaa_samples != 0; }
        bool is_created() const { return GLuint(_fbo) != 0; }

        void clear_color(fo::Vector4 color) { _fbo.clear_color(0, color); }
        void clear_depth(f32 d) { _fbo.clear_depth(d); }
    };

    struct GaussianBlurConstantsUB {
        u32 b_horizontal_or_vertical;

        f32 fb_width;
        f32 fb_height;

        f32 weights[5];
        f32 offsets[5];
    };

    struct BlurProgram {
        eng::VertexShaderHandle vs;
        eng::FragmentShaderHandle fs;
        eng::ShaderProgramHandle program;
    };

    struct PostProcess_GaussianBlur {
        u8 kernel_size = 9;
        pmr::vector<float> weights =
          pmr::vector<float>(eng::make_std_alloc<float>(eng::pmr_default_resource()));

        BlurProgram program_9_samples;
        BlurProgram program_5_lerped_samples;

        BlurProgram *current_program = &program_5_lerped_samples;

        eng::UniformBufferHandle ubo;
        GLuint ubo_bindpoint = 0;
        GLuint input_texture_bindpoint;

        // The FBO which we blur. Will also contain the final blurred result.
        FP16ColorFBO *input_fbo;

        // An FBO that records the first pass of the blur where we blur vertically
        ScratchUniquePtr<FP16ColorFBO> fbo_vertical_blur_dest = null_scratch_unique<FP16ColorFBO>();

        GaussianBlurConstantsUB ub;

        eng::ShaderDefines shader_defs;
    };

    static void init_blur_kernel_9(PostProcess_GaussianBlur &self)
    {
        self.weights = eng::gaussian_kernel(9);

        LOG_F(INFO, "Gaussian blur of length weights used");
        for (float weight : self.weights) {
            std::cout << weight << ", ";
        }

        std::cout << "\n";

        float normalized_texel_width = f32(1.0 / (double)self.input_fbo->width());
        float normalized_texel_height = f32(1.0 / (double)self.input_fbo->height());

        std::vector<float> weights(self.weights.begin(), self.weights.end());

        // Add the parameters to the shader defs
        self.shader_defs.add("GAUSSIAN_KERNEL_SIZE", (int)weights.size());
        self.shader_defs.add("GAUSSIAN_KERNEL_WEIGHTS", weights);
        self.shader_defs.add("NORMALIZED_TEXEL_WIDTH", normalized_texel_width);
        self.shader_defs.add("NORMALIZED_TEXEL_HEIGHT", normalized_texel_height);

        LOG_F(INFO, "Blur shader macros = \n%s", self.shader_defs.get_string().c_str());
    }

    // Special blur kernel requiring sampling only 5 texture sample calls per fragment
    static void init_lerped_kernel(PostProcess_GaussianBlur &self)
    {
        // This calculates the weight and normalized offset for sampling in between two consecutive texels
        // whose usual gaussian weights would be the given arguments.

        DEFINE_TRIVIAL_PAIR(WeightAndOffset, f64, weight, f64, offset);

        fn_ weight_and_offset = [](f64 gw_i, f64 gw_i1) {
            // Solve the two simple equations for w_n and a_n.
            f64 s = gw_i + gw_i1;
            f64 o_i = gw_i1 / s;
            f64 w_i = s;
            return WeightAndOffset{ w_i, o_i };
        };

        auto weights = eng::gaussian_kernel(9);

        WeightAndOffset w_and_o[5];
        w_and_o[0] = weight_and_offset(weights[0], weights[1]);
        w_and_o[1] = weight_and_offset(weights[2], weights[3]);
        w_and_o[2] = WeightAndOffset{ (f64)weights[4], 0.0 };
        w_and_o[3] = weight_and_offset(weights[5], weights[6]);
        w_and_o[4] = weight_and_offset(weights[7], weights[8]);

        // Shift the offsets so that they denote offset relative to the center sample (easier to mentally
        // visualize and write the code in the shader)
        w_and_o[0].offset = -4.0 + w_and_o[0].offset;
        w_and_o[1].offset = -2.0 + w_and_o[1].offset;
        w_and_o[2].offset = 0.0 + w_and_o[2].offset;
        w_and_o[3].offset = 1.0 + w_and_o[3].offset;
        w_and_o[4].offset = 3.0 + w_and_o[4].offset;

#if 0
    w_and_o[0].offset = -3.2307692308;
    w_and_o[1].offset = -1.3846153846;
    w_and_o[2].offset = 0.0;
    w_and_o[3].offset = 1.3846153846;
    w_and_o[4].offset = 3.2307692308;

    w_and_o[0].weight = 0.0702702703;
    w_and_o[1].weight = 0.3162162162;
    w_and_o[2].weight = 0.2270270270;
    w_and_o[3].weight = 0.3162162162;
    w_and_o[4].weight = 0.0702702703;

#endif

        // uniform float offset[3] = float[](0.0, 1.3846153846, 3.2307692308);
        // uniform float weight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

        // Store it in the cpu side ub, don't need to change it aftwards, just upload.
        for (int i = 0; i < 5; ++i) {
            self.ub.weights[i] = f32(w_and_o[i].weight);
            self.ub.offsets[i] = f32(w_and_o[i].offset);

            LOG_F(INFO, "Weights[%i] = %f, Offsets[%i] = %f", i, self.ub.weights[i], i, self.ub.offsets[i]);
        }

        self.ub.fb_width = (f32)self.input_fbo->width();
        self.ub.fb_height = (f32)self.input_fbo->height();

        LOG_F(INFO, "Fb width = %.1f, and dimensions = %.1f", self.ub.fb_width, self.ub.fb_height);
        {
            std::vector<float> v(5);
            for (zu i = 0; i < v.size(); ++i) {
                v[i] = w_and_o[i].offset;
            }
            self.shader_defs.add("LERPED_GAUSSIAN_OFFSETS", v);

            for (zu i = 0; i < v.size(); ++i) {
                v[i] = w_and_o[i].weight;
            }
            self.shader_defs.add("LERPED_GAUSSIAN_WEIGHTS", v);

            LOG_F(INFO, "Shader defs for kernel = \n%s", self.shader_defs.get_string().c_str());
        };
    }

    static void allocate_gl_resources(PostProcess_GaussianBlur &self)
    {
        // Create buffer
        eng::BufferCreateInfo buffer_ci{};
        buffer_ci.bytes = sizeof(GaussianBlurConstantsUB);
        buffer_ci.flags = eng::BufferCreateBitflags::SET_DYNAMIC_STORAGE;
        buffer_ci.name = "ubo@blur_constants";
        self.ubo = eng::create_uniform_buffer(eng::g_rm(), buffer_ci);

        self.shader_defs.add("GAUSSIAN_BLUR_UBO_BINDPOINT", (int)self.ubo_bindpoint);

        // Create the intermediate fbo for holding the vertical blur result.
        {
            FBOCreateConfig c;
            c.width = self.input_fbo->_width;
            c.height = self.input_fbo->_height;
            c.msaa_samples = self.input_fbo->msaa_samples();
            c.with_depth_texture = false;

            self.fbo_vertical_blur_dest = scratch_unique<FP16ColorFBO>();
            self.fbo_vertical_blur_dest->create(c);
        }

        // Bindpoint for the input texture
        self.input_texture_bindpoint = eng::g_bs().reserve_sampler_bindpoint();
        self.shader_defs.add("FP16_COLOR_TEXTURE_BINDPOINT", (int)self.input_texture_bindpoint);

        LOG_F(INFO, "Creating sampler for blur");

        // A sampler with border color of white
        eng::SamplerDesc sampler_desc = eng::default_sampler_desc;
        sampler_desc.min_filter = GL_LINEAR;
        sampler_desc.mag_filter = GL_LINEAR;
        sampler_desc.addrmode_u = sampler_desc.addrmode_v = sampler_desc.addrmode_w = GL_CLAMP_TO_BORDER;

        f32 white[4] = { XYZW(colors::Pink) };
        memcpy(sampler_desc.border_color, white, sizeof(white));

        GLuint sampler = eng::g_bs().get_sampler_object(sampler_desc);
        glBindSampler(self.input_texture_bindpoint, sampler);

        // Create the two programs, one with the vanilla 9 samples, the other with 5 lerped samples.

        fn_ create_blur_program = [&]() {
            BlurProgram prog;

            self.shader_defs.add("DO_VERTEX_SHADER");
            prog.vs = eng::create_shader_object(make_path(SOURCE_DIR, "data", "gaussian_blur_vsfs.glsl"),
                                                eng::ShaderKind::VERTEX_SHADER,
                                                self.shader_defs);
            self.shader_defs.remove("DO_VERTEX_SHADER").add("DO_FRAGMENT_SHADER");
            prog.fs = eng::create_shader_object(make_path(SOURCE_DIR, "data", "gaussian_blur_vsfs.glsl"),
                                                eng::ShaderKind::FRAGMENT_SHADER,
                                                self.shader_defs);

            eng::ShadersToUse use;
            use.vs = prog.vs.rmid();
            use.fs = prog.fs.rmid();
            prog.program = eng::link_shader_program(eng::g_rm(), use);

            // Inspect and get texture unit to bind to
            {
                eng::InspectedGLSL glsl(eng::get_gluint_from_rmid(g_rm(), prog.program.rmid()),
                                        eng::pmr_default_resource());

                const auto &uniforms = glsl.GetUniforms();

                auto it = std::find(STD_BEGIN_END(uniforms),
                                    [](const auto &u) { return u.fullName == "rgba16f_sampler"; });
                CHECK_F(it != uniforms.end(), "Did not find uniform info for rgba16f_sampler");
                auto sampler_info = MUST_OPTIONAL(it->optSamplerInfo);
                self.input_texture_bindpoint = sampler_info.textureUnit;
            }

            return prog;
        };

        self.program_9_samples = create_blur_program();
        self.shader_defs.add("USE_5_LERPED_SAMPLES");
        self.program_5_lerped_samples = create_blur_program();

        // Initialize the width and height

        LOG_F(INFO, "Allocated Gaussian Blur GL resources");
    }

    void init_pp_gaussian_blur(PostProcess_GaussianBlur &self, unsigned kernel_size, FP16ColorFBO *input_fbo)
    {
        self.kernel_size = u8(kernel_size);
        self.input_fbo = input_fbo;
        init_blur_kernel_9(self);
        init_lerped_kernel(self);
        allocate_gl_resources(self);
    }

    // Replace the input fbo with the
    void render_pp_gaussian_blur(PostProcess_GaussianBlur &self)
    {
        glUseProgram(gluint_from_globjecthandle(self.current_program->program));

        // ---- Vertical blur

        // Read FBO is the input fbo. Write FBO is the vertical blur dest fbo.
        self.input_fbo->fbo().bind_as_readable((GLuint)self.fbo_vertical_blur_dest->fbo());
        self.input_fbo->fbo().set_read_buffer(0);
        self.fbo_vertical_blur_dest->fbo().set_draw_buffers({ 0 });

        glBindTextureUnit(self.input_texture_bindpoint, self.input_fbo->fbo().color_attachment_texture(0));

        self.ub.b_horizontal_or_vertical = 1;

        glBindBuffer(GL_UNIFORM_BUFFER, gluint_from_globjecthandle(self.ubo));
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(self.ub), &self.ub);
        glBindVertexArray(eng::g_bs().no_attrib_vao());
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- Horizontal blur

        self.input_fbo->fbo().bind_as_writable((GLuint)self.fbo_vertical_blur_dest->fbo());
        self.input_fbo->fbo().set_draw_buffers({ 0 });
        self.fbo_vertical_blur_dest->fbo().set_read_buffer(0);

        glBindTextureUnit(self.input_texture_bindpoint,
                          self.fbo_vertical_blur_dest->fbo().color_attachment_texture(0));
        self.ub.b_horizontal_or_vertical = 0;
        glBindBuffer(GL_UNIFORM_BUFFER, gluint_from_globjecthandle(self.ubo));
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(self.ub), &self.ub);

        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void toggle_blur_technique(PostProcess_GaussianBlur &self)
    {
        if (self.current_program == &self.program_9_samples) {
            self.current_program = &self.program_5_lerped_samples;
            LOG_F(INFO, "Blur with lerped 5 samples");
        } else {
            self.current_program = &self.program_9_samples;
            LOG_F(INFO, "Blur with vanilla 9 samples");
        }
    }

} // namespace eng
