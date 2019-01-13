#define RENDERER_COMPILE

#if defined(RENDERER_COMPILE)

#    include <learnogl/glsl_inspect.h>
#    include <learnogl/typed_gl_resources.h>

#    include <scaffold/ordered_map.h>

#    include <learnogl/shader.h>

namespace eng {

static constexpr inline GLResource64
encode_glresource64(RMResourceID16 rmid, GLObjectKind::E object_kind, GLuint gl_handle) {
    GLResource64 b = 0;
    b |= GLResource64_RMID_Mask::set(b, rmid);
    b |= GLResource64_Kind_Mask::set(b, object_kind);
    b |= GLResource64_GLuint_Mask::set(b, gl_handle);
    return b;
}

RenderManager::RenderManager(fo::Allocator &backing_allocator)
    : CTOR_INIT_FIELD(_allocator, backing_allocator, 1 << 20)
    , CTOR_INIT_FIELD(_shader_paths, kilobytes(256), 1024)
    , CTOR_INIT_FIELD(_path_to_shader_rmid, fo::memory_globals::default_allocator()) {

    _kind_to_buffer[0] = nullptr;
    _kind_to_buffer[GLObjectKind::VERTEX_BUFFER] = &vertex_buffers;
    _kind_to_buffer[GLObjectKind::ELEMENT_ARRAY_BUFFER] = &element_array_buffers;
    _kind_to_buffer[GLObjectKind::UNIFORM_BUFFER] = &uniform_buffers;
    _kind_to_buffer[GLObjectKind::SHADER_STORAGE_BUFFER] = &storage_buffers;
    _kind_to_buffer[GLObjectKind::PIXEL_PACK_BUFFER] = &pixel_pack_buffers;
    _kind_to_buffer[GLObjectKind::PIXEL_UNPACK_BUFFER] = &pixel_unpack_buffers;

    fo::reserve(_deleted_ids, 32);

    // @rksht - Use a global configuration for these too.
    fo::reserve(_cached_rasterizer_states, 16);
    fo::reserve(_cached_depth_stencil_states, 16);
    fo::reserve(_cached_blendfunc_states, 16);

    // Put default states into the 0th index
    _cached_rasterizer_states[0].set_default();
    _cached_depth_stencil_states[0].set_default();
    _cached_blendfunc_states[0].set_default();
}

static RMResourceID16 new_resource_id(RenderManager &rm) {
    if (rm._deleted_ids._size == rm._deleted_ids._capacity) {
        RMResourceID16 rmid = fo::back(rm._deleted_ids);
        fo::pop_back(rm._deleted_ids);
        return rmid;
    }
    ++rm._num_allocated_ids;
    return rm._num_allocated_ids;
}

constexpr auto rm_to_gl_buffer_kind = gen_cexpr_sparse_array(GLObjectKind::VERTEX_BUFFER,
                                                             GL_ARRAY_BUFFER,
                                                             GLObjectKind::ELEMENT_ARRAY_BUFFER,
                                                             GL_ELEMENT_ARRAY_BUFFER,
                                                             GLObjectKind::UNIFORM_BUFFER,
                                                             GL_UNIFORM_BUFFER,
                                                             GLObjectKind::SHADER_STORAGE_BUFFER,
                                                             GL_SHADER_STORAGE_BUFFER,
                                                             GLObjectKind::ATOMIC_COUNTER_BUFFER,
                                                             GL_ATOMIC_COUNTER_BUFFER,
                                                             GLObjectKind::PIXEL_PACK_BUFFER,
                                                             GL_PIXEL_PACK_BUFFER,
                                                             GLObjectKind::PIXEL_UNPACK_BUFFER,
                                                             GL_PIXEL_UNPACK_BUFFER);

GLbitfield gl_buffer_access(BufferCreateFlags e) {
    GLbitfield b = 0;
    if (e & BufferCreateBitflags::SET_STATIC_STORAGE) {
        b |= (e & BufferCreateBitflags::USE_BUFFER_STORAGE) ? GL_CLIENT_STORAGE_BIT : GL_STATIC_DRAW;
    }
    if (e & BufferCreateBitflags::SET_CPU_READABLE) {
        b |= GL_MAP_READ_BIT;
    }
    if (e & BufferCreateBitflags::SET_CPU_WRITABLE) {
        b |= GL_MAP_WRITE_BIT;
    }
    if (e & BufferCreateBitflags::SET_PERSISTING) {
        b |= GL_MAP_PERSISTENT_BIT;
    }
    if (e & BufferCreateBitflags::SET_COHERENT) {
        b |= GL_MAP_COHERENT_BIT;
    }
    if (e & BufferCreateBitflags::SET_DYNAMIC_STORAGE) {
        b |= b |= (e & BufferCreateBitflags::USE_BUFFER_STORAGE) ? GL_DYNAMIC_STORAGE_BIT : GL_DYNAMIC_DRAW;
    }

    return b;
}

static RMResourceID16
create_buffer_common(RenderManager &rm, const BufferCreateInfo &ci, GLObjectKind::E buffer_kind) {
    GLenum gl_target = rm_to_gl_buffer_kind.get(buffer_kind);

    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(gl_target, buffer);

    const auto gl_flags = gl_buffer_access(ci.flags);

    if (ci.flags & BufferCreateBitflags::USE_BUFFER_STORAGE) {
        glBufferStorage(gl_target, ci.bytes, ci.init_data, gl_flags);
    } else {
        glBufferData(gl_target, ci.bytes, ci.init_data, gl_flags);
    }

    LET rmid = new_resource_id(rm);
    LET id64 = encode_glresource64(rmid, GLObjectKind::VERTEX_BUFFER, buffer);

    LET table = rm._kind_to_buffer[(u32)buffer_kind];
    fo::set(rm._rmid16_to_res64, rmid, id64);
    fo::set(*table, rmid, BufferInfo{ buffer, ci.bytes });

    fo::set(rm._buffer_sizes, rmid, ci.bytes);

    return rmid;
}

VertexBufferHandle create_vertex_buffer(RenderManager &rm, const BufferCreateInfo &ci) {
    return { create_buffer_common(rm, ci, GLObjectKind::VERTEX_BUFFER) };
}

ShaderStorageBufferHandle create_storage_buffer(RenderManager &rm, const BufferCreateInfo &ci) {
    return { create_buffer_common(rm, ci, GLObjectKind::SHADER_STORAGE_BUFFER) };
}

UniformBufferHandle create_uniform_buffer(RenderManager &rm, const BufferCreateInfo &ci) {
    return { create_buffer_common(rm, ci, GLObjectKind::UNIFORM_BUFFER) };
}

IndexBufferHandle create_element_array_buffer(RenderManager &rm, const BufferCreateInfo &ci) {
    return { create_buffer_common(rm, ci, GLObjectKind::ELEMENT_ARRAY_BUFFER) };
}

PixelPackBufferHandle create_pixel_pack_buffer(RenderManager &rm, const BufferCreateInfo &ci) {
    return { create_buffer_common(rm, ci, GLObjectKind::PIXEL_PACK_BUFFER) };
}

PixelUnpackBufferHandle create_pixel_unpack_buffer(RenderManager &rm, const BufferCreateInfo &ci) {
    return { create_buffer_common(rm, ci, GLObjectKind::PIXEL_UNPACK_BUFFER) };
}

VertexArrayHandle create_vao(RenderManager &self, const VaoFormatDesc &ci, const char *debug_label) {
    for (auto &vao : self._vaos_generated) {
        if (vao.format_desc == ci) {
            return vao.rmid;
        }
    }

    GLuint handle = make_vao(ci);
    set_vao_label(handle, debug_label);

    auto rmid = new_resource_id(self);
    auto id64 = encode_glresource64(rmid, GLObjectKind::VAO, handle);
    fo::set(self._rmid16_to_res64, rmid, id64);
    return VertexArrayHandle{ rmid };
}

RasterizerStateID create_rs_state(RenderManager &self, const RasterizerStateDesc &desc) {
    for (u32 i = 0; i < fo::size(self._cached_rasterizer_states); ++i) {
        auto &cached = self._cached_rasterizer_states[i];
        if (memcmp(&cached, &desc, sizeof(cached)) == 0) {
            return RasterizerStateID{ (u16)i };
        }
    }
    fo::push_back(self._cached_rasterizer_states, desc);
    return RasterizerStateID{ u16(fo::size(self._cached_rasterizer_states) - 1) };
}

DepthStencilStateID create_ds_state(RenderManager &self, const DepthStencilStateDesc &desc) {
    for (u32 i = 0; i < fo::size(self._cached_depth_stencil_states); ++i) {
        auto &cached = self._cached_depth_stencil_states[i];
        if (memcmp(&cached, &desc, sizeof(cached)) == 0) {
            return DepthStencilStateID{ (u16)i };
        }
    }
    fo::push_back(self._cached_depth_stencil_states, desc);
    return DepthStencilStateID{ u16(fo::size(self._cached_depth_stencil_states) - 1) };
}

BlendFunctionStateID create_blendfunc_state(RenderManager &self, const BlendFunctionDesc &desc) {
    for (u32 i = 0; i < fo::size(self._cached_blendfunc_states); ++i) {
        auto &cached = self._cached_blendfunc_states[i];
        if (memcmp(&cached, &desc, sizeof(cached)) == 0) {
            return BlendFunctionStateID{ (u16)i };
        }
    }
    fo::push_back(self._cached_blendfunc_states, desc);
    return BlendFunctionStateID{ u16(fo::size(self._cached_blendfunc_states) - 1) };
}

// clang-format off
inline constexpr auto gen_external_format_table() {
    // static constexpr u32 num_types = log2_ceil(TexelOrigType::COUNT) + log2_ceil(TexelComponents::COUNT);

    constexpr auto left_shift = ENUM_BITS(TexelOrigType);

    constexpr u32 num_unique_client_types =
        TexelOrigType::numbits * TexelComponents::numbits * TexelInterpretType::numbits;

    CexprSparseArray<GLExternalFormat, num_unique_client_types> types;

    // Unnormalized fetches
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::FLOAT, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::FLOAT, TexelComponents::RG, TexelInterpretType::UNNORMALIZED),{ GL_RG, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::FLOAT, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::FLOAT, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED),{ GL_RGBA, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED_INTEGER, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG_INTEGER, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB_INTEGER, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA_INTEGER, GL_UNSIGNED_INT});
    // Normalized fetches
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_RED, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U8, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), {GL_RGBA, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_RED, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U16, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), { GL_RGBA, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_RED, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelOrigType::U32, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), { GL_RGBA, GL_UNSIGNED_INT});

    // Note that depth textures usually are never provided by the application but received as a result of a
    // rendering. As such it doesn't make sense to generate an external format for it.

    return types;
}

// clang-format on

constexpr bool fetch_and_client_compatible(TexelOrigType::E client_type,
                                           TexelComponents::E client_components,
                                           TexelInterpretType::E interpret_type,
                                           TexelFetchType::E fetch_type) {
    bool ok = true;

    ok = ok &&
         (IMPLIES(interpret_type == TexelInterpretType::NORMALIZED, fetch_type == TexelFetchType::FLOAT));

    ok = ok && IMPLIES((interpret_type == TexelInterpretType::UNNORMALIZED &&
                        TexelOrigType::is_integer(client_type)),
                       fetch_type != TexelFetchType::FLOAT);

    return ok;
}

reallyconst gl_texel_client_type_table = gen_external_format_table();

void init_render_manager(RenderManager &self, const RenderManagerInitConfig &conf) {
    // Create a camera transform uniform buffer
    BufferCreateInfo buffer_ci;
    buffer_ci.bytes = CAMERA_TRANSFORM_UBLOCK_SIZE;
    buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
    buffer_ci.name = "@camera_transform_ubo";
    self._camera_ubo_handle = create_uniform_buffer(self, buffer_ci);

    // Initialize default framebuffer info.
    self._screen_fbo.init_from_default_framebuffer();
}

VarShaderHandle create_shader_object(RenderManager &self,
                                     const fs::path &shader_source_file,
                                     ShaderKind shader_kind,
                                     const ShaderDefines &macro_defs,
                                     const char *debug_label) {
    assert(GLObjectKind::is_shader((GLObjectKind::E)shader_kind));

    ShaderInfo shader_info = {};

    std::string path_u8string = shader_source_file.generic_u8string();

    fo_ss::Buffer shader_path_ss;
    fo_ss::push(shader_path_ss, path_u8string.c_str(), fo_ss::length(shader_path_ss));

    // Check if shader is already available in the map. Create and add if not present
    auto it = fo::get(self._path_to_shader_rmid, shader_path_ss);

    ::eng::ShaderSourceType shader_source = shader_source_file;

    RMResourceID16 rmid = 0;

    if (it == fo::end(self._path_to_shader_rmid)) {
        shader_info.handle =
            ::eng::create_shader_object(shader_source, (ShaderKind)shader_kind, macro_defs, debug_label);
        shader_info.path_str_index = self._shader_paths.add(path_u8string.c_str(), path_u8string.size());

        rmid = new_resource_id(self);
        GLResource64 res64 = encode_glresource64(rmid, (GLObjectKind::E)shader_kind, shader_info.handle);
        self._rmid16_to_res64[rmid] = res64;

        self._shaders[rmid] = shader_info;

    } else {
        rmid = it->v;
        auto lookup = find_with_end(self._rmid16_to_res64, rmid);
        if (lookup.not_found()) {
            auto ss = shader_short_string(shader_source);
            ABORT_F("Mapping error for shader object with shader_source - %s", fo::string_stream::c_str(ss));
        }
        shader_info.handle = GLResource64_GLuint_Mask::extract(lookup.keyvalue().second());
    }

    switch (shader_kind) {
    case ShaderKind::VERTEX_SHADER:
        return VertexShaderHandle(rmid);
    case ShaderKind::FRAGMENT_SHADER:
        return FragmentShaderHandle(rmid);
    case ShaderKind::TESS_CONTROL_SHADER:
        return TessControlShaderHandle(rmid);
    case ShaderKind::TESS_EVAL_SHADER:
        return TessEvalShaderHandle(rmid);
    case ShaderKind::GEOMETRY_SHADER:
        return GeometryShaderHandle(rmid);
    case ShaderKind::COMPUTE_SHADER:
        return ComputeShaderHandle(rmid);
    default:
        CHECK_F(false, "Unreachable");
        return {};
    }
}

TU_LOCAL VarShaderHandle check_shader_macro_and_create(RenderManager &rm,
                                                       const fs::path &shader_source_file,
                                                       const char *shader_specific_extension,
                                                       ShaderKind shader_kind,
                                                       ShaderDefines &macro_defs,
                                                       const char *debug_label,
                                                       const char *macro) {

    static constexpr std::array<const char *, 6> shader_type_macros = {
        "DO_VERTEX_SHADER",    "DO_FRAGMENT_SHADER", "DO_TESS_CONTROL_SHADER",
        "DO_TESS_EVAL_SHADER", "DO_GEOMETRY_SHADER", "DO_COMPUTE_SHADER"
    };

    for (auto &macro : shader_type_macros) {
        if (macro_defs.has(macro)) {
            macro_defs.remove(macro);
        }
    }

    if (strcmp(shader_source_file.extension().generic_u8string().c_str(), shader_specific_extension) != 0) {
        macro_defs.add(macro, 1);
    }

    return create_shader_object(rm, shader_source_file, shader_kind, macro_defs, debug_label);
}

VertexShaderHandle create_vertex_shader(RenderManager &self,
                                        const fs::path &shader_source_file,
                                        ShaderDefines &macro_defs,
                                        const char *debug_label) {

    auto variant_shader = check_shader_macro_and_create(self,
                                                        shader_source_file,
                                                        ".vert",
                                                        ShaderKind::VERTEX_SHADER,
                                                        macro_defs,
                                                        debug_label,
                                                        "DO_VERTEX_SHADER");
    return get_value<VertexShaderHandle>(variant_shader);
}

FragmentShaderHandle create_fragment_shader(RenderManager &self,
                                            const fs::path &shader_source_file,
                                            ShaderDefines &macro_defs,
                                            const char *debug_label) {
    auto variant_shader = check_shader_macro_and_create(self,
                                                        shader_source_file,
                                                        ".frag",
                                                        ShaderKind::FRAGMENT_SHADER,
                                                        macro_defs,
                                                        debug_label,
                                                        "DO_FRAGMENT_SHADER");
    return get_value<FragmentShaderHandle>(variant_shader);
}

TessControlShaderHandle create_tess_control_shader(RenderManager &self,
                                                   const fs::path &shader_source_file,
                                                   ShaderDefines &macro_defs,
                                                   const char *debug_label) {

    auto variant_shader = check_shader_macro_and_create(self,
                                                        shader_source_file,
                                                        ".tessc",
                                                        ShaderKind::TESS_CONTROL_SHADER,
                                                        macro_defs,
                                                        debug_label,
                                                        "DO_TESS_CONTROL_SHADER");

    return get_value<TessControlShaderHandle>(variant_shader);
}

TessEvalShaderHandle create_tess_eval_shader(RenderManager &self,
                                             const fs::path &shader_source_file,
                                             ShaderDefines &macro_defs,
                                             const char *debug_label) {

    auto variant_shader = check_shader_macro_and_create(self,
                                                        shader_source_file,
                                                        ".tesse",
                                                        ShaderKind::TESS_EVAL_SHADER,
                                                        macro_defs,
                                                        debug_label,
                                                        "DO_TESS_EVAL_SHADER");

    return get_value<TessEvalShaderHandle>(variant_shader);
}

GeometryShaderHandle create_geometry_shader(RenderManager &self,
                                            const fs::path &shader_source_file,
                                            ShaderDefines &macro_defs,
                                            const char *debug_label) {

    auto variant_shader = check_shader_macro_and_create(self,
                                                        shader_source_file,
                                                        ".geom",
                                                        ShaderKind::GEOMETRY_SHADER,
                                                        macro_defs,
                                                        debug_label,
                                                        "DO_GEOMETRY_SHADER");

    return get_value<GeometryShaderHandle>(variant_shader);
}

ComputeShaderHandle create_compute_shader(RenderManager &self,
                                          const fs::path &shader_source_file,
                                          ShaderDefines &macro_defs,
                                          const char *debug_label) {

    auto variant_shader = check_shader_macro_and_create(self,
                                                        shader_source_file,
                                                        ".comp",
                                                        ShaderKind::COMPUTE_SHADER,
                                                        macro_defs,
                                                        debug_label,
                                                        "DO_COMPUTE_SHADER");

    return get_value<ComputeShaderHandle>(variant_shader);
}

static GLResource64 link_new_program(RenderManager &self,
                                     const ShadersToUse &shaders_to_use,
                                     _ShaderStageKey key,
                                     const char *debug_label) {
    GLuint h = glCreateProgram();

    bool is_compute_program = false;

    // Do a check that if it's a compute shader, you should not specify any other shaders.
    if (shaders_to_use.cs != 0) {
        CHECK_EQ_F(shaders_to_use.vs, 0);
        CHECK_EQ_F(shaders_to_use.fs, 0);
        CHECK_EQ_F(shaders_to_use.tc, 0);
        CHECK_EQ_F(shaders_to_use.te, 0);
        CHECK_EQ_F(shaders_to_use.gs, 0);
        is_compute_program = true;
    }

    LOCAL_FUNC get_gluint = [&](RMResourceID16 rmid) -> GLuint {
        auto lookup = find_with_end(self._rmid16_to_res64, rmid);
        if (lookup.not_found()) {
            return 0;
        }
        return (GLuint)GLResource64_GLuint_Mask::extract(lookup.keyvalue().second());
    };

    LOCAL_FUNC attach_shader = [&](RMResourceID16 rmid) {
        if (rmid != 0) {
            GLuint shader_gluint = get_gluint(rmid);
            CHECK_NE_F(shader_gluint, 0, "rmid '%u' does not point to a live shader object", rmid);
            glAttachShader(h, shader_gluint);
        }
    };

    attach_shader(shaders_to_use.vs);
    attach_shader(shaders_to_use.fs);
    attach_shader(shaders_to_use.tc);
    attach_shader(shaders_to_use.te);
    attach_shader(shaders_to_use.gs);
    attach_shader(shaders_to_use.cs);
    glLinkProgram(h);

    // Aborts if it did not link
    check_program_status(h);
    set_program_label(h, debug_label);

    RMResourceID16 rmid = new_resource_id(self);

    GLResource64 res64 = encode_glresource64(
        rmid, is_compute_program ? GLObjectKind::COMPUTE_PROGRAM : GLObjectKind::GRAPHICS_PROGRAM, h);
    self._linked_shaders[key] = res64;

    return res64;
}

void set_ds_state(RenderManager &rm, DepthStencilStateID state_id) {
    auto &desc = rm._cached_depth_stencil_states[state_id._id];
    set_gl_depth_stencil_state(desc);
}

void set_rs_state(RenderManager &rm, RasterizerStateID state_id) {
    auto &desc = rm._cached_rasterizer_states[state_id._id];
    set_gl_rasterizer_state(desc);
}

#    if 0
void set_blendfunc_state(RenderManager &rm, BlendFunctionStateID state_id) {
    auto &desc = rm._cached_blendfunc_states[state_id._id];
    set_gl_blendfunc_state(desc);
}
#    endif

namespace internal {

// Links shader programs if they are not already.
TU_LOCAL GLResource64 link_shader_program(RenderManager &self,
                                          const ShadersToUse &shaders_to_use,
                                          const char *debug_label) {
    // Check if we have the shader program already linked
    _ShaderStageKey key = shaders_to_use.key();

    auto lookup = find_with_end(self._linked_shaders, key);

    GLResource64 res64 = 0;

    if (lookup.not_found()) {
        res64 = link_new_program(self, shaders_to_use, key, debug_label);
    } else {
        res64 = lookup.keyvalue().second();
    }
    return res64;
}

} // namespace internal

RMResourceID16
link_shader_program(RenderManager &rm, const ShadersToUse &shaders_to_use, const char *debug_label) {
    GLResource64 res64 = internal::link_shader_program(rm, shaders_to_use, debug_label);
    return (RMResourceID16)GLResource64_RMID_Mask::extract(res64);
}

RMResourceID16 set_shaders(RenderManager &self, const ShadersToUse &shaders_to_use, const char *debug_label) {
    GLResource64 res64 = internal::link_shader_program(self, shaders_to_use, debug_label);
    glUseProgram(GLResource64_GLuint_Mask::extract(res64));
    return (RMResourceID16)GLResource64_RMID_Mask::extract(res64);
}

void bind_to_bindpoint(RenderManager &self, UniformBufferHandle ubo_handle, GLuint bindpoint) {
    auto res64_lookup = find_with_end(self.uniform_buffers, ubo_handle._rmid);
    CHECK_F(bool(res64_lookup));
    GLuint h = GLResource64_GLuint_Mask::extract(res64_lookup.keyvalue().second().handle);

    // Buffer size
    auto size_lookup = find_with_end(self._buffer_sizes, ubo_handle._rmid);
    CHECK_F(bool(size_lookup));

    u32 size = size_lookup.keyvalue().second();

    glBindBuffer(GL_UNIFORM_BUFFER, h);
    glBindBufferRange(GL_UNIFORM_BUFFER, bindpoint, h, 0, size);
}

#    if 0
struct NewFBO {
    FBOConfig _config;
    std::array<GLResource64, MAX_FBO_ATTACHMENTS> _color_textures = {};
    GLResource64 _depth_texture = 0;
    GLResource64 _fbo_id64 = 0;

    // Set this to the clear color you want
    fo::Vector4 clear_color = eng::math::zero_4;
    f32 clear_depth = -1.0f;

    u32 num_color_textures() const {
        u32 count = 0;
        for (auto id64 : _color_textures) {
            count += u32(GLResource64_RMID_Mask::extract(id64) != 0);
        }
        return count;
    }

    bool has_depth_texture() const { return GLResource64_RMID_Mask::extract(_depth_texture) != 0; }
};

#    endif

void create_fbo(RenderManager &self,
                const fo::Array<RMResourceID16> &color_textures,
                RMResourceID16 depth_texture,
                const char *debug_label) {
    FBO fbo_with_glhandle;
    fbo_with_glhandle.gen(debug_label);

    for (u32 i = 0; i < fo::size(color_textures); ++i) {
        auto rmid16 = color_textures[i];

        auto lookup = find_with_end(self._rmid16_to_res64, rmid16);

        CHECK_F(lookup.found(), "Did not find any gl texture created with rmid = %u", u32(rmid16));

        GLResource64 res64 = lookup.keyvalue().second();
        GLuint gl_handle = GLResource64_GLuint_Mask::extract(res64);
        fbo_with_glhandle.add_attachment(i, gl_handle);
    }

    if (depth_texture != 0) {
        auto lookup = find_with_end(self._rmid16_to_res64, depth_texture);
        CHECK_F(lookup.found(), "Did not find any gl texture created with rmid = %u", u32(depth_texture));

        GLResource64 res64 = lookup.keyvalue().second();
        GLuint gl_handle = GLResource64_GLuint_Mask::extract(res64);
        fbo_with_glhandle.add_depth_attachment(gl_handle);
    }

    fbo_with_glhandle.set_done_creating();

    // Store the texture config in _config field.
    // ---

    RMResourceID16 id16 = new_resource_id(self);
    encode_glresource64(id16, GLObjectKind::FRAMEBUFFER, fbo_with_glhandle._fbo_handle);
}

// Impl of miscellaneous pure helper functions

// Initialize a VAO format description from the given mesh data.
VaoFormatDesc vao_format_from_mesh_data(const mesh::StrippedMeshData &m) {
    VaoAttributeFormat position(3, GL_FLOAT, GL_FALSE, m.position_offset, 0, 0);
    VaoAttributeFormat normal(3, GL_FLOAT, GL_FALSE, m.normal_offset, 0, 0);
    VaoAttributeFormat texcoord2d(2, GL_FLOAT, GL_FALSE, m.tex2d_offset, 0, 0);
    VaoAttributeFormat tangent(4, GL_FLOAT, GL_FALSE, m.tangent_offset, 0, 0);

    fo::TempAllocator512 ta;
    fo::Array<VaoAttributeFormat> array(ta);
    fo::reserve(array, 4);

    fo::push_back(array, position);

    if (m.normal_offset != mesh::ATTRIBUTE_NOT_PRESENT) {
        fo::push_back(array, normal);
    }
    if (m.tex2d_offset != mesh::ATTRIBUTE_NOT_PRESENT) {
        fo::push_back(array, texcoord2d);
    }
    if (m.tangent_offset != mesh::ATTRIBUTE_NOT_PRESENT) {
        fo::push_back(array, tangent);
    }
    return VaoFormatDesc::from_attribute_formats(array);
}

} // namespace eng

#endif
