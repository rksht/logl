#include <learnogl/dds_loader.h>
#include <learnogl/gl_misc.h>
#include <learnogl/shader.h>
#include <learnogl/stb_image.h>
#include <learnogl/typed_gl_resources.h>

#include <scaffold/ordered_map.h>

#include <thread> // call_once

namespace eng {

TU_LOCAL void create_common_vaos(RenderManager &self);

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

    // First entry of the `_fbos` array is just a default constructed NewFBO, denoting default framebuffer.
    fo::push_back(_fbos, {});
}

void shutdown_render_manager(RenderManager &self) { UNUSED(self); }

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
        b |= (e & BufferCreateBitflags::USE_BUFFER_STORAGE) ? GL_DYNAMIC_STORAGE_BIT : GL_DYNAMIC_DRAW;
    }

    return b;
}

static RMResourceID16
create_buffer_common(RenderManager &rm, const BufferCreateInfo &ci, GLObjectKind::E buffer_kind) {
    GLenum gl_target = rm_to_gl_buffer_kind.get(buffer_kind);

    GLuint gl_handle = 0;
    glGenBuffers(1, &gl_handle);
    glBindBuffer(gl_target, gl_handle);

    const auto gl_flags = gl_buffer_access(ci.flags);

    if (ci.flags & BufferCreateBitflags::USE_BUFFER_STORAGE) {
        glBufferStorage(gl_target, ci.bytes, ci.init_data, gl_flags);
    } else {
        glBufferData(gl_target, ci.bytes, ci.init_data, gl_flags);
    }

    const auto rmid = new_resource_id(rm);
    const auto id64 = encode_glresource64(rmid, buffer_kind, gl_handle);

    const auto table_p = rm._kind_to_buffer[(u32)buffer_kind];
    fo::set(rm._rmid16_to_res64, rmid, id64);

    fo::set(*table_p, rmid, BufferInfo{ gl_handle, ci.bytes });
    fo::set(rm._buffer_sizes, rmid, ci.bytes);

    eng::set_buffer_label(gl_handle, ci.name);

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

void source_to_uniform_buffer(RenderManager &self, UniformBufferHandle ubo, SourceToBufferInfo source_info) {
    const BufferInfo &buffer_info = find_with_end(self.uniform_buffers, ubo.rmid())
                                        .keyvalue_must("No buffer info found for uniform buffer")
                                        .second();
    glBindBuffer(GL_UNIFORM_BUFFER, buffer_info.handle);

    if (source_info.num_bytes == 0) {
        source_info.num_bytes = buffer_info.bytes;
    }

    if (source_info.discard) {
        glInvalidateBufferSubData(buffer_info.handle, source_info.byte_offset, source_info.num_bytes);
    }
    glBufferSubData(
        GL_UNIFORM_BUFFER, source_info.byte_offset, source_info.num_bytes, source_info.source_bytes);
}

const char *ShaderInfo::pathname() const { return eng::g_strings().get(this->path_str_index); }

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

// TODO: Cache. Already in gl_binding_state.cpp. Do that here.
SamplerObjectHandle create_sampler_object(RenderManager &self, const SamplerDesc &desc) {
    GLuint gl_handle;
    glGenSamplers(1, &gl_handle);

    LOG_F(INFO, "Creating a new sampler");

    if (desc.mag_filter != default_sampler_desc.mag_filter) {
        glSamplerParameteri(gl_handle, GL_TEXTURE_MAG_FILTER, desc.mag_filter);
    }

    if (desc.min_filter != default_sampler_desc.min_filter) {
        glSamplerParameteri(gl_handle, GL_TEXTURE_MIN_FILTER, desc.min_filter);
    }

    if (desc.addrmode_u != default_sampler_desc.addrmode_u) {
        glSamplerParameteri(gl_handle, GL_TEXTURE_WRAP_S, desc.addrmode_u);
    }

    if (desc.addrmode_v != default_sampler_desc.addrmode_v) {
        glSamplerParameteri(gl_handle, GL_TEXTURE_WRAP_T, desc.addrmode_v);
    }

    if (desc.addrmode_w != default_sampler_desc.addrmode_w) {
        glSamplerParameteri(gl_handle, GL_TEXTURE_WRAP_R, desc.addrmode_w);
    }

    // Set the border color unconditionally
    glSamplerParameterfv(gl_handle, GL_TEXTURE_BORDER_COLOR, desc.border_color);

    if (desc.min_lod != default_sampler_desc.min_lod) {
        glSamplerParameterf(gl_handle, GL_TEXTURE_MIN_LOD, desc.min_lod);
    }

    if (desc.max_lod != default_sampler_desc.max_lod) {
        glSamplerParameterf(gl_handle, GL_TEXTURE_MAX_LOD, desc.max_lod);
    }

    if (desc.compare_mode != GL_NONE) {
        // Set the compare func and mode together
        glSamplerParameteri(gl_handle, GL_TEXTURE_COMPARE_MODE, desc.compare_mode);
        glSamplerParameteri(gl_handle, GL_TEXTURE_COMPARE_FUNC, desc.compare_func);
    }

    if (desc.mip_lod_bias != default_sampler_desc.mip_lod_bias) {
        glSamplerParameterf(gl_handle, GL_TEXTURE_LOD_BIAS, desc.mip_lod_bias);
    }

    if (desc.max_anisotropy != 0.0f) {
        glSamplerParameterf(gl_handle, GL_TEXTURE_MAX_ANISOTROPY, desc.max_anisotropy);
    }

    auto rmid = new_resource_id(self);
    auto res64 = encode_glresource64(rmid, GLObjectKind::SAMPLER_OBJECT, gl_handle);
    self._rmid16_to_res64[rmid] = res64;
    return SamplerObjectHandle(rmid);
}

RasterizerStateId create_rs_state(RenderManager &self, const RasterizerStateDesc &desc) {
    for (u32 i = 0; i < fo::size(self._cached_rasterizer_states); ++i) {
        auto &cached = self._cached_rasterizer_states[i];
        if (memcmp(&cached, &desc, sizeof(cached)) == 0) {
            return RasterizerStateId{ (u16)i };
        }
    }
    fo::push_back(self._cached_rasterizer_states, desc);
    return RasterizerStateId{ u16(fo::size(self._cached_rasterizer_states) - 1) };
}

DepthStencilStateId create_ds_state(RenderManager &self, const DepthStencilStateDesc &desc) {
    for (u32 i = 0; i < fo::size(self._cached_depth_stencil_states); ++i) {
        auto &cached = self._cached_depth_stencil_states[i];
        if (memcmp(&cached, &desc, sizeof(cached)) == 0) {
            return DepthStencilStateId{ (u16)i };
        }
    }
    fo::push_back(self._cached_depth_stencil_states, desc);
    return DepthStencilStateId{ u16(fo::size(self._cached_depth_stencil_states) - 1) };
}

BlendFunctionDescId create_blendfunc_state(RenderManager &self, const BlendFunctionDesc &desc) {
    for (u32 i = 0; i < fo::size(self._cached_blendfunc_states); ++i) {
        auto &cached = self._cached_blendfunc_states[i];
        if (memcmp(&cached, &desc, sizeof(cached)) == 0) {
            return BlendFunctionDescId{ (u16)i };
        }
    }
    fo::push_back(self._cached_blendfunc_states, desc);
    return BlendFunctionDescId{ u16(fo::size(self._cached_blendfunc_states) - 1) };
}

constexpr u32 num_texel_type_configs =
    TexelBaseType::numbits * TexelComponents::numbits * TexelInterpretType::numbits;

// clang-format off

constexpr auto make_texel_info_to_ext_format() {
    CexprSparseArray<GLExternalFormat, num_texel_type_configs> types;

    // Unnormalized fetches
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::RG, TexelInterpretType::UNNORMALIZED),{ GL_RG, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB, GL_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED),{ GL_RGBA, GL_FLOAT });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED, GL_HALF_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RG, GL_HALF_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RGB, GL_HALF_FLOAT });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RGBA, GL_HALF_FLOAT });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA_INTEGER, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA_INTEGER, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RED_INTEGER, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG_INTEGER, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB_INTEGER, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA_INTEGER, GL_UNSIGNED_INT});
    // Normalized fetches
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_RED, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), {GL_RGBA, GL_UNSIGNED_BYTE });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_RED, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), { GL_RGBA, GL_UNSIGNED_SHORT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_RED, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB, GL_UNSIGNED_INT});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), { GL_RGBA, GL_UNSIGNED_INT});

    // Note that depth textures usually are never provided by the application but received as a result of a
    // rendering. As such it doesn't make sense to generate an external format for it.

    return types;
}

constexpr CexprSparseArray<GLExternalFormat, num_texel_type_configs> texel_info_to_gl_external_format = make_texel_info_to_ext_format();

constexpr auto make_texel_info_to_int_format() {
    CexprSparseArray<GLInternalFormat, num_texel_type_configs> types;

    // Unnormalized fetches
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::R, TexelInterpretType::UNNORMALIZED), {GL_R32F});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::RG, TexelInterpretType::UNNORMALIZED),{ GL_RG32F});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB32F });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED),{ GL_RGBA32F });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_R16F});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RG16F });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RGB16F });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_RGB32F });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_R8UI});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG8UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB8UI  });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA8UI });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_R16UI});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG16UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB16UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA16UI });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::R, TexelInterpretType::UNNORMALIZED), { GL_R32UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RG, TexelInterpretType::UNNORMALIZED), { GL_RG32UI});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGB, TexelInterpretType::UNNORMALIZED), { GL_RGB32UI});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGBA, TexelInterpretType::UNNORMALIZED), { GL_RGBA32UI });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::FLOAT, TexelComponents::DEPTH, TexelInterpretType::UNNORMALIZED), { GL_DEPTH_COMPONENT32F });

    // Normalized fetches
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_R8 });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG8 });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB8 });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U8, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), {GL_RGBA8 });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_R16});
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG16 });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB16 });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), { GL_RGBA16 });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::R, TexelInterpretType::NORMALIZED), { GL_R32UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RG, TexelInterpretType::NORMALIZED), { GL_RG32UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGB, TexelInterpretType::NORMALIZED), { GL_RGB32UI });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::RGBA, TexelInterpretType::NORMALIZED), { GL_RGBA32UI });

    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U32, TexelComponents::DEPTH24_STENCIL8, TexelInterpretType::NORMALIZED), { GL_DEPTH24_STENCIL8 });
    types.set(_ENCODE_TEXEL_INFO(TexelBaseType::U16, TexelComponents::DEPTH, TexelInterpretType::NORMALIZED), { GL_DEPTH_COMPONENT16 });

    return types;
}

constexpr CexprSparseArray<GLInternalFormat, num_texel_type_configs> texel_info_to_gl_internal_format = make_texel_info_to_int_format();

// clang-format on

constexpr bool sampler_type_and_client_type_ok(TexelBaseType::E client_type,
                                               TexelComponents::E client_components,
                                               TexelInterpretType::E interpret_type,
                                               TexelSamplerType::E sampler_type) {
    bool ok = true;

    ok = ok &&
         (IMPLIES(interpret_type == TexelInterpretType::NORMALIZED, sampler_type == TexelSamplerType::FLOAT));

    ok = ok && IMPLIES((interpret_type == TexelInterpretType::UNNORMALIZED &&
                        TexelBaseType::is_integer(client_type)),
                       sampler_type != TexelSamplerType::FLOAT);

    return ok;
}

Texture2DHandle create_texture_2d(RenderManager &self, TextureCreateInfo texture_ci) {
    std::once_flag once_flag;

    std::call_once(once_flag, []() {
        for (size_t i = 0; i < texel_info_to_gl_external_format.HASH_SLOTS; ++i) {
            const auto &ext_format = texel_info_to_gl_external_format.items[i];

            if (ext_format.index == 0u) {
                continue;
            }

            printf("Encoded texel info = %u, External format (components, type) = %u, %u\n",
                   ext_format.index,
                   ext_format.value.components,
                   ext_format.value.component_type);
        }

        printf("-------------------------------\n");
    });

    LOG_F(INFO,
          "texture_ci.width,height,depth = %u, %u, %u",
          texture_ci.width,
          texture_ci.height,
          texture_ci.depth);

    LOG_IF_F(WARNING,
             texture_ci.depth != 1,
             "Called %s but given texture_ci.depth == (%u) != 1. Ignoring the value.",
             __PRETTY_FUNCTION__,
             texture_ci.depth);

    u8 *source = nullptr;
    GLuint gl_handle = 0;

    glGenTextures(1, &gl_handle);
    glBindTexture(GL_TEXTURE_2D, gl_handle);

    const auto texel_info = texture_ci.texel_info;

    const auto encoded_texel_info =
        _ENCODE_TEXEL_INFO(texel_info.internal_type, texel_info.components, texel_info.interpret_type);

    LOG_F(INFO,
          "internal_type = %u, components = %u, interpret_type = %u, encoded = %u",
          texel_info.internal_type,
          texel_info.components,
          texel_info.interpret_type,
          encoded_texel_info);

    GLInternalFormat internal_format = texel_info_to_gl_internal_format.get_maybe_nil(encoded_texel_info);
    if (internal_format.e == 0) {
        ABORT_F("Not a valid texel info");
        print_callstack();
    }

    GLExternalFormat external_format = texel_info_to_gl_external_format.get_maybe_nil(encoded_texel_info);

    LOG_F(INFO,
          "external format - components = %u, type = %u, encoded_texel_info = %u",
          external_format.components,
          external_format.component_type,
          encoded_texel_info);

    if (texture_ci.source.contains_subtype<u8 *>()) {
        source = texture_ci.source.get_value<u8 *>();

        glTexStorage2D(
            GL_TEXTURE_2D, texture_ci.mips, internal_format.e, texture_ci.width, texture_ci.height);

        if (texture_ci.mips > 1 && source != nullptr) {
            ABORT_F("Not suppporting initializing mips using a single source");
        }

        if (source != nullptr) {
            CHECK_F(!external_format.invalid(),
                    "Not a valid texel info - no corresponding external format - required for TexSubImage2D");
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            0,
                            0,
                            texture_ci.width,
                            texture_ci.height,
                            external_format.components,
                            external_format.component_type,
                            source);
        }

    } else if (texture_ci.source.contains_subtype<fs::path>()) {
        LOG_F(INFO,
              "external format - components = %u, type = %u",
              external_format.components,
              external_format.component_type);

        CHECK_F(!external_format.invalid(), "Not a valid texel info - no corresponding external format");

        const fs::path &file_path = texture_ci.source.get_value<fs::path>();
        const auto str_file = file_path.u8string();

        if (!fs::exists(file_path)) {
            ABORT_F("Failed to load image - file does not exist - '%s'", str_file.c_str());
        }

        if (file_path.extension() == ".png") {
            int w = 0, h = 0, channels_in_file = 0;
            int expected_channels = num_channels_for_components(texel_info.components);

            u8 *image = stbi_load(str_file.c_str(), &w, &h, &channels_in_file, expected_channels);

            if (channels_in_file != expected_channels) {
                ABORT_F("Failed to load png - '%s' - expected num channels = %i, but image has - %i",
                        str_file.c_str(),
                        expected_channels,
                        channels_in_file);
            }

            if (!image) {
                ABORT_F("Failed to load png - '%s' for some reason idk", str_file.c_str());
            }

            texture_ci.width = (u16)w;
            texture_ci.height = (u16)h;
            texture_ci.depth = 1;
            texture_ci.mips = 1;

            const auto internal_format =
                channels_in_file == 1
                    ? GL_R8
                    : channels_in_file == 2 ? GL_RG8 : channels_in_file == 3 ? GL_RGB8 : GL_RGBA8;

            glTexStorage2D(GL_TEXTURE_2D, 1, internal_format, (GLuint)w, (GLuint)h);

            LOG_F(INFO,
                  "External format - components = %u, component_type = %u",
                  external_format.components,
                  external_format.component_type);

            // Load
            //
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            0,
                            0,
                            texture_ci.width,
                            texture_ci.height,
                            external_format.components,
                            external_format.component_type,
                            image);

        } else if (file_path.extension() == "dds") {
            dds::load_texture(file_path, gl_handle, nullptr, &fo::memory_globals::default_allocator());
            ABORT_F("Unimplemented. Should fill up texture_ci here");
        } else {
            ABORT_F("Unrecognized texture file extension - %s", file_path.extension().u8string().c_str());
        }
    } else {
        LOG_F(INFO, "Created empty texture"); // TODO: Remove this.
    }

    RMResourceID16 rmid16 = new_resource_id(self);

    LOG_F(INFO, "Created texture with rmid = %u", rmid16);

    TextureInfo info;
    info.rmid = rmid16;
    info.texture_kind = GLObjectKind::TEXTURE_2D;
    info.width = texture_ci.width;
    info.height = texture_ci.height;
    info.depth = 1;
    info.mips = texture_ci.mips;

    info.internal_type = texel_info.internal_type;

    info.sampler_type = texel_info.interpret_type == TexelInterpretType::NORMALIZED
                            ? TexelSamplerType::FLOAT
                            : TexelBaseType::is_float(texel_info.internal_type)
                                  ? TexelSamplerType::FLOAT
                                  : TexelBaseType::is_signed(texel_info.internal_type)
                                        ? TexelSamplerType::SIGNED_INT
                                        : TexelBaseType::is_unsigned(texel_info.internal_type)
                                              ? TexelSamplerType::UNSIGNED_INT
                                              : TexelSamplerType::INVALID;

    self.texture_infos[rmid16] = info;

    LOG_F(INFO, "gl_handle = %u", gl_handle);
    self._rmid16_to_res64[rmid16] = encode_glresource64(rmid16, GLObjectKind::TEXTURE_2D, gl_handle);
    CHECK_EQ_F((GLuint)GLResource64_GLuint_Mask::extract(self._rmid16_to_res64[rmid16]), gl_handle);

    return Texture2DHandle{ rmid16 };
}

void init_render_manager(RenderManager &self) {
    // Create a camera transform uniform buffer
    BufferCreateInfo buffer_ci;
    buffer_ci.bytes = CAMERA_TRANSFORM_UBLOCK_SIZE;
    buffer_ci.flags = BufferCreateBitflags::SET_DYNAMIC_STORAGE;
    buffer_ci.name = "@camera_transform_ubo";
    self._camera_ubo_handle = create_uniform_buffer(self, buffer_ci);

    // Initialize default framebuffer info.
    self._screen_fbo.init_from_default_framebuffer();

    create_common_vaos(self);
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
                                     ShaderProgramKey key,
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

const ShadersToUse get_shaders_used_by_program(const RenderManager &self,
                                               const ShaderProgramHandle &program) {

    ShadersToUse use;
    for (const auto &entry : self._linked_shaders) {
        if ((u16)GLResource64_RMID_Mask::extract(entry.second()) == program.rmid()) {
            const auto key = entry.first();
            use.vs = (key.k0 & 0xffff0000u) >> 16;
            use.fs = (key.k0 & 0x0000ffffu);
            use.tc = (key.k1 & 0xffff0000u) >> 16;
            use.te = (key.k1 & 0x0000ffffu);
            use.gs = (key.k2 & 0xffff0000u) >> 16;
            use.cs = (key.k2 & 0x0000ffffu);
        }
    }
    return use;
}

fo_ss::Buffer ShadersToUse::source_paths_as_string(const RenderManager &rm) const {
    using namespace fo_ss;

    static_assert(sizeof(ShadersToUse) == sizeof(u16) * 6, "Need this for the following");
    const auto rmid_array = reinterpret_cast<const u16 *>(this);

    Buffer ss;

    const char *types[] = { "vs", "tc", "te", "gs", "fs", "cs" };

    for (u32 i = 0; i < 6; ++i) {
        if (rmid_array[i] == 0) {
            continue;
        }

        find_with_end(rm._shaders, rmid_array[i]).if_found([&](const auto _, const ShaderInfo &info) {
            ss << types[i] << ": " << rm._shader_paths.get(info.path_str_index) << "\n";
        });
    }

    return ss;
}

void set_ds_state(RenderManager &rm, DepthStencilStateId state_id) {
    auto &desc = rm._cached_depth_stencil_states[state_id._id];
    set_gl_depth_stencil_state(desc);
}

void set_rs_state(RenderManager &rm, RasterizerStateId state_id) {
    auto &desc = rm._cached_rasterizer_states[state_id._id];
    set_gl_rasterizer_state(desc);
}

void set_blendfunc_state(RenderManager &rm, i32 output_number, BlendFunctionDescId &state_id) {
    auto &desc = rm._cached_blendfunc_states[state_id._id];

    if (desc.blend_op == BlendOp::BLEND_DISABLED) {
        glDisablei(GL_BLEND, output_number);
        return;
    }

    glEnablei(GL_BLEND, output_number);
    glBlendFuncSeparatei(output_number,
                         desc.src_rgb_factor,
                         desc.dst_rgb_factor,
                         desc.src_alpha_factor,
                         desc.dst_alpha_factor);
    glBlendEquationi(output_number, desc.blend_op);
}

namespace internal {

// Links shader programs if they are not already.
TU_LOCAL GLResource64 link_shader_program(RenderManager &self,
                                          const ShadersToUse &shaders_to_use,
                                          const char *debug_label) {
    // Check if we have the shader program already linked
    ShaderProgramKey key = shaders_to_use.key();

    auto lookup = find_with_end(self._linked_shaders, key);

    GLResource64 res64 = 0;

    if (lookup.not_found()) {
        res64 = link_new_program(self, shaders_to_use, key, debug_label);
        self._rmid16_to_res64[(u16)GLResource64_RMID_Mask::extract((res64))] = res64;
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

void set_program(RenderManager &self, const ShaderProgramHandle handle) {
    GLuint gl_handle = get_gluint_from_rmid(self, handle.rmid());
    glUseProgram(gl_handle);
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

FboId create_default_fbo(RenderManager &self, const fo::Array<AttachmentAndClearValue> &clear_attachments) {
    NewFBO &fbo = self._fbos[0];
    for (auto &a : clear_attachments) {
        fbo.clear_attachment_after_bind(a.attachment, a.clear_value);
    }
    return FboId{ 0 };
}

FboId create_fbo(RenderManager &self,
                 const fo::Array<RMResourceID16> &color_textures,
                 RMResourceID16 depth_texture,
                 const fo::Array<AttachmentAndClearValue> &clear_attachments,
                 const char *debug_label) {
    FBO fbo_with_glhandle;
    fbo_with_glhandle.gen(debug_label).bind_as_writable();

    NewFBO new_fbo;

    fo::TempAllocator256 ta;
    fo::Array<const TextureInfo *> texture_infos(ta);

    for (u32 i = 0; i < fo::size(color_textures); ++i) {
        auto rmid16 = color_textures[i];

        // 0 denotes no texture backing this color attachment.
        if (rmid16 == 0) {
            continue;
        }

        auto lookup = find_with_end(self._rmid16_to_res64, rmid16);

        CHECK_F(lookup.found(), "Did not find any gl texture created with rmid = %u", u32(rmid16));

        GLResource64 res64 = lookup.keyvalue().second();
        GLuint gl_handle = GLResource64_GLuint_Mask::extract(res64);
        fbo_with_glhandle.add_attachment(i, gl_handle);

        new_fbo._color_textures[i] = res64;

        auto &texture_info = find_with_end(self.texture_infos, rmid16).keyvalue_must().second();
        fo::push_back(texture_infos, &texture_info);
    }

    if (depth_texture != 0) {
        auto lookup = find_with_end(self._rmid16_to_res64, depth_texture);

        CHECK_F(
            lookup.found(), "Did not find any gl depth texture created with rmid = %u", u32(depth_texture));

        GLResource64 res64 = lookup.keyvalue().second();
        GLuint gl_handle = GLResource64_GLuint_Mask::extract(res64);
        fbo_with_glhandle.add_depth_attachment(gl_handle);

        new_fbo._depth_texture = res64;

        auto &texture_info = find_with_end(self.texture_infos, depth_texture).keyvalue_must().second();
        fo::push_back(texture_infos, &texture_info);
    }

    fbo_with_glhandle.set_done_creating();

    RMResourceID16 id16 = new_resource_id(self);
    new_fbo._fbo_id64 = encode_glresource64(id16, GLObjectKind::FRAMEBUFFER, fbo_with_glhandle._fbo_handle);

    // Store the per attachment config.

    for (u32 i = 0; i < fo::size(texture_infos); ++i) {
        const TextureInfo *texture_info = texture_infos[i];
        if (!texture_info) {
            continue;
        }

        auto &attachment_info = i == fo::size(color_textures) ? new_fbo._dims.depth_attachment_dim
                                                              : new_fbo._dims.color_attachment_dims[i];

        attachment_info.width = texture_info->width;
        attachment_info.height = texture_info->height;
        attachment_info.sampler_type = texture_info->sampler_type;
        attachment_info.internal_type = texture_info->internal_type;
    }

    fo::push_back(self._fbos, new_fbo);

    for (const auto &a : clear_attachments) {
        new_fbo.clear_attachment_after_bind(a.attachment, a.clear_value);
    }

    return FboId{ (u16)(fo::size(self._fbos) - 1) };
}

void bind_destination_fbo(eng::RenderManager &rm,
                          FboId fbo_id,
                          const ::StaticVector<i32, MAX_FRAGMENT_OUTPUTS> &attachment_map) {
    const NewFBO &fbo = rm._fbos[fbo_id._id];

    StaticVector<GLenum, MAX_FRAGMENT_OUTPUTS> gl_attachment_numbers;
    gl_attachment_numbers.fill_backing_array(GL_NONE);

    const u32 num_color_textures = fbo.num_color_textures();

    for (size_t i = 0; i < attachment_map.filled_size(); ++i) {
        const i32 attachment_number = attachment_map.unchecked_at(i);

        if (attachment_number != -1) {
            DCHECK_LT_F(attachment_number,
                        (i32)num_color_textures,
                        "No texture backing attachment number %i",
                        attachment_number);
        }

        const GLenum gl_attachment_number =
            attachment_number == -1 ? GL_NONE : GL_COLOR_ATTACHMENT0 + (GLenum)attachment_number;

        gl_attachment_numbers.push_back(gl_attachment_number);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLResource64_GLuint_Mask::extract(fbo._fbo_id64));
    glDrawBuffers(MAX_FRAGMENT_OUTPUTS, gl_attachment_numbers.data());

    // TODO: Check if this attachment is actually bound as a draw buffer. Otherwise this can fail. Not too
    // important though.

    for (u32 i = 0; i < fbo._attachments_to_clear.filled_size(); ++i) {
        const auto &a = fbo._attachments_to_clear[i];
        glClearBufferfv(a.attachment == -1 ? GL_DEPTH : GL_COLOR,
                        a.attachment == -1 ? 0 : a.attachment,
                        reinterpret_cast<const GLfloat *>(&a.clear_value));
    }
}

void bind_source_fbo(eng::RenderManager &rm, FboId fbo_id, i32 attachment_number) {
    const NewFBO &fbo = rm._fbos[fbo_id._id];

    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLResource64_GLuint_Mask::extract(fbo._fbo_id64));

    if (attachment_number != -1) {
        DCHECK_F(attachment_number < (i32)fbo.num_color_textures());

        // If this is the default framebuffer, we ignore the attachment number
        glReadBuffer(fbo_id._id == 0 ? GL_BACK_LEFT : GL_COLOR_ATTACHMENT0 + (GLenum)attachment_number);
    }
}

NewFBO &NewFBO::clear_attachment_after_bind(i32 attachment_number, const fo::Vector4 &color_value) {
    _attachments_to_clear.push_back({ attachment_number, color_value });
    return *this;
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

TU_LOCAL void create_common_vaos(RenderManager &self) {
    // Position 2D
    self.pos2d_vao = create_vao(
        self, VaoFormatDesc::from_attribute_formats({ { 2, GL_FLOAT, GL_FALSE, 0, 0 } }), "@vao_pos2d");

    // Position 3D
    self.pos_vao = create_vao(
        self, VaoFormatDesc::from_attribute_formats({ { 3, GL_FLOAT, GL_FALSE, 0, 0 } }), "@vao_pos3d");

    // Pos, Normal
    self.pn_vao = create_vao(
        self,
        VaoFormatDesc::from_attribute_formats(
            { { 3, GL_FLOAT, GL_FALSE, 0, 0 }, { 3, GL_FLOAT, GL_FALSE, sizeof(fo::Vector3), 0 } }),
        "@vao_pos_normal_3d");

    // Pos, Normal, Texcoord2d
    self.pnu_vao =
        create_vao(self,
                   VaoFormatDesc::from_attribute_formats(
                       { { 3, GL_FLOAT, GL_FALSE, 0, 0 },
                         { 3, GL_FLOAT, GL_FALSE, sizeof(fo::Vector3), 0 },
                         { 2, GL_FLOAT, GL_FALSE, sizeof(fo::Vector3) + sizeof(fo::Vector2), 0 } }),
                   "@vao_pos_normal_uv_3d");

    // Pos, Normal, Texcoord2d, Tangent
    self.pnut_vao = create_vao(self,
                               VaoFormatDesc::from_attribute_formats(
                                   { { 3, GL_FLOAT, GL_FALSE, 0, 0 },
                                     { 3, GL_FLOAT, GL_FALSE, sizeof(fo::Vector3), 0 },
                                     { 2, GL_FLOAT, GL_FALSE, sizeof(fo::Vector3) + sizeof(fo::Vector3), 0 },
                                     { 4,
                                       GL_FLOAT,
                                       GL_FALSE,
                                       sizeof(fo::Vector3) + sizeof(fo::Vector3) + sizeof(fo::Vector2),
                                       0 } }),
                               "@vao_pos_normal_uv_tan_3d");
}

} // namespace eng
