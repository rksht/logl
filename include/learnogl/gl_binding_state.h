// Basic resource binding management so I don't have to worry (too much) about managing binding point related
// stuff from the demos.

// Core goals - I *do not* want to make too intelligent of a renderer. That's very time consuming to do. I
// want to make editing the main constructs easy. I would like to operate with the core GL constructs and no
// layer on top.

// One thing that I want to structure the resource types around is that no resource "owns another resource via
// C++ destructors". The whole thing is managed by the application and/or by the GL BindingState object

#pragma once

#include <learnogl/essential_headers.h>

#include <learnogl/string_table.h>
#include <scaffold/arena_allocator.h>
#include <scaffold/collection_types.h>
#include <scaffold/const_log.h>
#include <scaffold/dypackeduintarray.h>
#include <scaffold/murmur_hash.h>
#include <scaffold/non_pods.h>
#include <scaffold/pod_hash.h>
#include <scaffold/temp_allocator.h>
#include <scaffold/vector.h>

#include <assert.h>
#include <unordered_map>

namespace eng {

//#if !defined NDEBUG
#define LOGL_OBJECT_LABEL(type, gluint, name) glObjectLabel(type, gluint, (GLsizei)strlen(name), name)
// #else
// #    define LOGL_OBJECT_LABEL(type, gluint, name)
// #endif

#define DEFINE_SET_LABEL_FUNC(func_name, object_type, name)                                                  \
    inline void func_name(GLuint object, const char *name) {                                                 \
        if (name) {                                                                                          \
            LOGL_OBJECT_LABEL(object_type, object, name);                                                    \
        };                                                                                                   \
    }

// Associate debug labels to opengl objects using the following functions for getting better debug output
// messages.
DEFINE_SET_LABEL_FUNC(set_buffer_label, GL_BUFFER, name)
DEFINE_SET_LABEL_FUNC(set_shader_label, GL_SHADER, name)
DEFINE_SET_LABEL_FUNC(set_program_label, GL_PROGRAM, name)
DEFINE_SET_LABEL_FUNC(set_vao_label, GL_VERTEX_ARRAY, name)
DEFINE_SET_LABEL_FUNC(set_query_label, GL_QUERY, name)
DEFINE_SET_LABEL_FUNC(set_transfdbk_label, GL_TRANSFORM_FEEDBACK, name)
DEFINE_SET_LABEL_FUNC(set_sampler_label, GL_SAMPLER, name)
DEFINE_SET_LABEL_FUNC(set_texture_label, GL_TEXTURE, name)
DEFINE_SET_LABEL_FUNC(set_rbo_label, GL_RENDERBUFFER, name)
DEFINE_SET_LABEL_FUNC(set_fbo_label, GL_FRAMEBUFFER, name)

namespace max_bindings_wanted {

constexpr GLint texture_image_units = 16;
constexpr GLint uniform_buffer = 32;
constexpr GLint shader_storage_buffer = 8;

} // namespace max_bindings_wanted

// Sampler Description. The integer types used to represent the fields can be compressed. But not bothering
// with that.
struct SamplerDesc {
    GLenum mag_filter;
    GLenum min_filter;
    GLenum addrmode_u;
    GLenum addrmode_v;
    GLenum addrmode_w;
    f32 border_color[4];
    GLenum compare_mode;
    GLenum compare_func;
    f32 mip_lod_bias;
    f32 max_anisotropy;
    f32 min_lod;
    f32 max_lod;
};

// Equals is not strictly needed, but whatever.
inline bool operator==(const SamplerDesc &s1, const SamplerDesc &s2) {
    return memcmp(&s1, &s2, sizeof(SamplerDesc)) ==
           0; // xyspoon: Potentially wrong if we are using dynamically calculated float values lod bias,
              // border color, etc.
}
// Less than operator, since we will be caching these descriptions in an orderedmap.
inline bool operator<(const SamplerDesc &s1, const SamplerDesc &s2) {
    return memcmp(&s1, &s2, sizeof(SamplerDesc)) < 0; // Same warning as above.
}
inline bool operator>(const SamplerDesc &s1, const SamplerDesc &s2) { return s2 < s1; }
inline u64 hash(const SamplerDesc &s) { return fo::murmur_hash_64(&s, sizeof(SamplerDesc), 0xdeadc0de); }

// A sampler description that has its field initialized to the default state of newly created samplers. Copy
// this, and edit to create a custom sampler.
constexpr SamplerDesc default_sampler_desc = { GL_LINEAR, GL_NEAREST_MIPMAP_LINEAR,
                                               GL_REPEAT, GL_REPEAT,
                                               GL_REPEAT, { 0.0f, 0.0f, 0.0f, 0.0f },
                                               GL_NONE,   GL_LEQUAL,
                                               0.0f,
                                               0.0f, // 1.0f to MAX_ANISOTROPY denotes a valid range
                                               -1000.0f,  1000.0f };

// Set the in-built sampler parameters of the texture
void set_texture_parameters(GLuint texture, const SamplerDesc &sampler_desc);

// Rasterizer state description.
struct RasterizerStateDesc {
    GLenum cull_side;  // GL_FRONT, GL_BACK, GL_FRONT_AND_BACK, or GL_NONE
    GLenum front_face; // GL_CCW or GL_CW
    GLenum fill_mode;  // GL_POINT, GL_LINE, or GL_FILL
    bool enable_depth_clamp;
    bool enable_scissor;
    bool enable_multisample;
    bool enable_line_smoothing;
    i32 constant_depth_bias;
    f32 slope_scaled_depth_bias;

    inline void set_default();
};

// Just a test. Doesn't make sense to compress rasterizer state because they don't ever change per object
// frequently.
struct RasterizerStateDescCompressed {
    u32 _flags;
    i32 _constant_depth_bias;
    f32 _slope_scaled_depth_bias;

    using _cull_side_mask = IntMask<0, 2, u32>;
    using _front_face_mask = IntMask<2, 1, u32>;
    using _fill_mode_mask = IntMask<3, 2, u32>;
    using _enable_depth_clamp_mask = IntMask<5, 1, u32>;
    using _enable_scissor_mask = IntMask<6, 1, u32>;
    using _enable_multisample_mask = IntMask<7, 1, u32>;
    using _enable_line_smoothing_mask = IntMask<8, 1, u32>;

    GLenum cull_side() const {
        return UintSequenceSwitch<0, GLenum, GL_FRONT, GL_BACK, GL_FRONT_AND_BACK, GL_NONE>::get(
            _cull_side_mask::extract(_flags), __PRETTY_FUNCTION__);
    }

    GLenum front_face() const {
        return UintSequenceSwitch<2, GLenum, GL_CCW, GL_CW>::get(_front_face_mask::extract(_flags),
                                                                 __PRETTY_FUNCTION__);
    }

    GLenum fill_mode() const {
        return UintSequenceSwitch<3, GLenum, GL_POINT, GL_LINE, GL_FILL>::get(
            _fill_mode_mask::extract(_flags), __PRETTY_FUNCTION__);
    }

    GLboolean enable_depth_clamp() const {
        return UintSequenceSwitch<5, u32, 0, 1>::get(_enable_depth_clamp_mask::extract(_flags),
                                                     __PRETTY_FUNCTION__)
                   ? GL_TRUE
                   : GL_FALSE;
    }

    GLboolean enable_scissor_mask() const {
        return UintSequenceSwitch<6, u32, 0, 1>::get(_enable_scissor_mask::extract(_flags),
                                                     __PRETTY_FUNCTION__)
                   ? GL_TRUE
                   : GL_FALSE;
    }

    GLboolean enable_multisample() const {
        return (GLboolean)UintSequenceSwitch<7, u32, 0, 1>::get(_enable_multisample_mask::extract(_flags),
                                                                __PRETTY_FUNCTION__);
    }

    GLboolean enable_line_smoothing() const {
        return (GLboolean)UintSequenceSwitch<8, u32, 0, 1>::get(_enable_line_smoothing_mask::extract(_flags),
                                                                __PRETTY_FUNCTION__);
    }

    i32 constant_depth_bias() const { return _constant_depth_bias; }

    f32 slope_scaled_depth_bias() const { return _slope_scaled_depth_bias; }
};

// Somewhat easier to use blend factors. The functional equivalent is glBlendFuncSeparate and
// glBlendEquationSeparate. You can set the blend factor. Separate allows specifying a different factor for
// the alpha components of the src and dest. Not supporting GL_CONSTANT_COLOR and GL_CONSTANT_ALPHA as
// factors.

enum BlendFactor : GLenum {
    ZERO = GL_ZERO,
    ONE = GL_ONE,
    SRC_COLOR = GL_SRC_COLOR,
    ONE_MINUS_SRC_COLOR = GL_ONE_MINUS_SRC_COLOR,
    SRC_ALPHA = GL_SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA = GL_ONE_MINUS_SRC_ALPHA,
    DST_ALPHA = GL_DST_ALPHA,
    ONE_MINUS_DST_ALPHA = GL_ONE_MINUS_DST_ALPHA,
    DST_COLOR = GL_DST_COLOR,
    ONE_MINUS_DST_COLOR = GL_ONE_MINUS_DST_COLOR,
    SRC_ALPHA_SATURATE = GL_SRC_ALPHA_SATURATE,
};

enum BlendOp : GLenum {
    FUNC_ADD = GL_FUNC_ADD,
    FUNC_SUBTRACT = GL_FUNC_SUBTRACT,
    FUNC_REVERSE_SUBTRACT = GL_FUNC_REVERSE_SUBTRACT,
    MAX = GL_MAX,
    MIN = GL_MIN,
    BLEND_DISABLED = MIN + 1,
};

struct BlendFunctionDesc {
    BlendFactor src_rgb_factor; // 4 bits are ok for the factors
    BlendFactor dst_rgb_factor;
    BlendFactor src_alpha_factor;
    BlendFactor dst_alpha_factor;
    BlendOp blend_op; // 3 bits are needed for the blend op. Let's use 4 bits anyway.

    inline void set_default();
};

namespace logl_internal {

constexpr BlendFunctionDesc make_default_blendfunc_state() {
    BlendFunctionDesc config{};
    config.src_rgb_factor = BlendFactor::ONE;
    config.dst_rgb_factor = BlendFactor::ONE;
    config.src_alpha_factor = BlendFactor::ONE;
    config.dst_alpha_factor = BlendFactor::ONE;
    config.blend_op = BlendOp::BLEND_DISABLED;
    return config;
}

struct CompressedBlendFunctionDesc {
    u32 _bits;

    CompressedBlendFunctionDesc() = default;
    explicit operator u32() const { return _bits; }
};

using SrcRgbFactor_Mask = Mask32<0, 4>;
using DstRgbFactor_Mask = Mask32<4, 4>;
using SrcAlphaFactor_Mask = Mask32<8, 4>;
using DstAlphaFactor_Mask = Mask32<12, 4>;
using BlendOp_Mask = Mask32<16, 4>;

inline u32 compressed_blend_config(const BlendFunctionDesc &conf) {
    LOCAL_FUNC compress_factor = [](BlendFactor factor) -> u32 {
        return factor == BlendFactor::ZERO ? 0 : factor == BlendFactor::ONE ? 1 : (factor & 0x1) + 2;
    };

    LOCAL_FUNC compress_blend_op = [](BlendOp op) {
        // See glad.h. The blend ops are of the form 0x80[]. Can extract the last 16 bits and get it.
        return 0xf & op;
    };

    u32 b = 0;

    b = SrcRgbFactor_Mask::set(b, compress_factor(conf.src_rgb_factor));
    b = DstRgbFactor_Mask::set(b, compress_factor(conf.dst_rgb_factor));
    b = SrcAlphaFactor_Mask::set(b, compress_factor(conf.src_alpha_factor));
    b = DstAlphaFactor_Mask::set(b, compress_factor(conf.dst_alpha_factor));
    b = BlendOp_Mask::set(b, compress_blend_op(conf.blend_op));
    return b;
}

inline BlendFunctionDesc uncompressed_blend_config(u32 c) {
    LOCAL_FUNC uncompress_factor = [](u32 compressed_factor) -> BlendFactor {
        GLenum r = compressed_factor == 0
                       ? BlendFactor::ZERO
                       : compressed_factor == 1 ? BlendFactor::ONE : (compressed_factor - 2) | 0x300u;
        return static_cast<BlendFactor>(r);
    };

    LOCAL_FUNC uncompressed_blend_op = [](u32 compressed_op) -> BlendOp {
        return static_cast<BlendOp>(0x800u | compressed_op);
    };

    BlendFunctionDesc s;
    s.src_rgb_factor = uncompress_factor(SrcRgbFactor_Mask::extract(c));
    s.dst_rgb_factor = uncompress_factor(DstRgbFactor_Mask::extract(c));
    s.src_alpha_factor = uncompress_factor(SrcAlphaFactor_Mask::extract(c));
    s.dst_alpha_factor = uncompress_factor(DstAlphaFactor_Mask::extract(c));
    s.blend_op = uncompressed_blend_op(BlendOp_Mask::extract(c));

    return s;
}

} // namespace logl_internal

// Default blend config is simply what is called additive blending. Just add the source and dest colors.
constexpr BlendFunctionDesc default_blendfunc_state = logl_internal::make_default_blendfunc_state();

inline void BlendFunctionDesc::set_default() { *this = default_blendfunc_state; }

namespace logl_internal {

constexpr RasterizerStateDesc make_default_rasterizer_desc() {
    RasterizerStateDesc rs{};
    rs.cull_side = GL_BACK;
    rs.front_face = GL_CCW;
    rs.fill_mode = GL_FILL;
    rs.enable_depth_clamp = true;
    rs.enable_scissor = false;
    rs.enable_multisample = false;
    rs.enable_line_smoothing = false;
    rs.constant_depth_bias = 0;
    rs.slope_scaled_depth_bias = 0.0f;
    return rs;
}

} // namespace logl_internal

// Default rasterizer state
constexpr RasterizerStateDesc default_rasterizer_state_desc = logl_internal::make_default_rasterizer_desc();

inline void RasterizerStateDesc::set_default() { *this = default_rasterizer_state_desc; }

inline NOT_CONSTEXPR_IN_MSVC bool operator==(const RasterizerStateDesc &rs1, const RasterizerStateDesc &rs2) {
    return memcmp(&rs1, &rs2, sizeof(RasterizerStateDesc)) == 0;
}

inline NOT_CONSTEXPR_IN_MSVC bool operator<(const RasterizerStateDesc &rs1, const RasterizerStateDesc &rs2) {
    return memcmp(&rs1, &rs2, sizeof(RasterizerStateDesc)) < 0;
}

inline NOT_CONSTEXPR_IN_MSVC bool operator>(const RasterizerStateDesc &rs1, const RasterizerStateDesc &rs2) {
    return rs2 < rs1;
}

inline u64 hash(const RasterizerStateDesc &rs) {
    return fo::murmur_hash_64(&rs, sizeof(RasterizerStateDesc), 0xdeadbeefu);
}

// Stencil buffer operation can be defined either for both front and back faces, or individual for the two
// types of faces.
struct StencilOps {
    GLenum fail;
    GLenum pass;
    GLenum pass_but_dfail;
    GLenum compare_func;
    u8 stencil_ref;
    u8 stencil_read_mask;
};

struct DepthStencilStateDesc {
    GLboolean enable_depth_test;
    GLboolean enable_depth_write;
    GLenum depth_compare_func;

    GLboolean enable_stencil_test;

    u8 stencil_write_mask;

    StencilOps front_face_ops;
    StencilOps back_face_ops;

    inline void set_default();
};

namespace logl_internal {

constexpr DepthStencilStateDesc make_default_depth_stencil_state_desc() {
    DepthStencilStateDesc d = {};
    d.enable_depth_test = GL_TRUE;
    d.enable_depth_write = GL_TRUE;
    d.depth_compare_func = GL_LESS;

    d.enable_stencil_test = GL_FALSE;
    d.stencil_write_mask = 255;

    d.front_face_ops.fail = GL_KEEP;
    d.front_face_ops.pass = GL_KEEP;
    d.front_face_ops.pass_but_dfail = GL_KEEP;
    d.front_face_ops.compare_func = GL_ALWAYS;
    d.front_face_ops.stencil_ref = 255;
    d.front_face_ops.stencil_read_mask = 255;

    d.back_face_ops = d.front_face_ops;

    return d;
}

} // namespace logl_internal

constexpr DepthStencilStateDesc default_depth_stencil_desc =
    logl_internal::make_default_depth_stencil_state_desc();

inline void DepthStencilStateDesc::set_default() { *this = default_depth_stencil_desc; }

union CreateDeleterArguments {
    GLenum texture_target;
};

// Wraps a GLuint that denotes a live GL object.
template <typename Subclass> struct GLResourceDeleter : NonCopyable {
    GETONLY(GLuint, handle) = 0;

    GLResourceDeleter() = default;

    GLResourceDeleter(GLuint handle)
        : _handle(handle) {}

    GLResourceDeleter(GLResourceDeleter &&o)
        : _handle(o._handle) {
        o._handle = 0;
    }

    GLResourceDeleter &operator=(GLResourceDeleter &&o) {
        _sub()->destroy();
        _handle = o._handle;
        o._handle = 0;
        return *this;
    }

    // Set the resource handle explicitly.
    void set(GLuint handle) {
        CHECK_EQ_F(_handle, 0, "Called set(GLuint handle) on already created resource, _handle != 0");
        _handle = handle;
    }

    // Create the resource
    void check_create(const CreateDeleterArguments &args = {}) {
        CHECK_EQ_F(_handle, 0u, "Called create() on already created resource, _handle != 0");
    }

    // Destroy the resource
    void check_destroy() { _handle = 0; }

    Subclass *_sub() { return static_cast<Subclass *>(this); }
    Subclass *_sub() const { return static_cast<const Subclass *>(this); }

    bool is_created() const { return _handle != 0; }

    ~GLResourceDeleter() { _sub()->destroy(); }
};

struct BufferDeleter : GLResourceDeleter<BufferDeleter> {
    BufferDeleter() = default;
    BufferDeleter(GLuint handle)
        : GLResourceDeleter<BufferDeleter>(handle) {}

    void create(const CreateDeleterArguments &args = {}) {
        glCreateBuffers(1, &_handle);
        GLResourceDeleter<BufferDeleter>::check_create();
    }

    void destroy() {
        if (is_created()) {
            glDeleteBuffers(1, &_handle);
            GLResourceDeleter<BufferDeleter>::check_destroy();
        }
    }
};

struct VertexBufferDeleter final : BufferDeleter {};
struct IndexBufferDeleter final : BufferDeleter {};
struct ShaderStorageBufferDeleter final : BufferDeleter {};
struct UniformBufferDeleter final : BufferDeleter {};
struct AtomicCounterBufferDeleter final : BufferDeleter {};
struct IndirectBufferDeleter final : BufferDeleter {};

struct TextureDeleter : GLResourceDeleter<TextureDeleter> {
    TextureDeleter() = default;
    TextureDeleter(GLuint handle)
        : GLResourceDeleter<TextureDeleter>(handle) {}

    void create(const CreateDeleterArguments &args = {}) {
        glCreateTextures(args.texture_target, 1, &_handle);
        GLResourceDeleter<TextureDeleter>::check_create();
    }

    void destroy() {
        if (is_created()) {
            glDeleteTextures(1, &_handle);
            GLResourceDeleter<TextureDeleter>::check_destroy();
        }
    }
};

struct VertexArrayDeleter : GLResourceDeleter<VertexArrayDeleter> {
    VertexArrayDeleter() = default;

    VertexArrayDeleter(GLuint handle)
        : GLResourceDeleter<VertexArrayDeleter>(handle) {}

    void create(const CreateDeleterArguments &args = {}) {
        glCreateVertexArrays(1, &_handle);
        GLResourceDeleter<VertexArrayDeleter>::check_create();
    }

    void destroy() {
        if (is_created()) {
            glDeleteVertexArrays(1, &_handle);
            GLResourceDeleter<VertexArrayDeleter>::check_destroy();
        }
    }
};

// Keeping the compute shader and program together
struct ComputeShaderAndProg {
    GLuint shader = 0;
    GLuint program = 0;
};

namespace gl_desc {

struct ResourceHandle {
    GLuint _handle = 0;

    ResourceHandle() = default;

    explicit ResourceHandle(GLuint handle)
        : _handle(handle) {}

    GLuint handle() const { return _handle; }

    bool invalid() { return _handle == 0; }
};

/// Represents a texture
struct SampledTexture : ResourceHandle {
    SampledTexture() = default;

    explicit SampledTexture(GLuint handle)
        : ResourceHandle(handle) {}

    u64 hash() const { return _handle; }
};

inline bool operator==(const SampledTexture &t1, const SampledTexture &t2) {
    return t1.handle() == t2.handle();
}

/// Represents a (uniform buffer, offset, size) combination
struct UniformBuffer : ResourceHandle {
    u32 _offset = 0;
    u32 _size = 0;

    UniformBuffer() = default;

    explicit UniformBuffer(GLuint handle, u32 offset, u32 size)
        : ResourceHandle(handle)
        , _offset(offset)
        , _size(size) {}

    u64 hash() const { return (u64(handle()) << 32) + _size + _offset; }

    // Returns an 'invalid' i.e. nil description. Used for the hash table.
    static UniformBuffer get_invalid() { return UniformBuffer(0, 0, 0); }
};

inline bool operator==(const UniformBuffer &ub1, const UniformBuffer &ub2) {
    return ub1._handle == ub2._handle && ub1._offset == ub2._offset && ub1._size == ub2._size;
}

/// Represents a (storage buffer, offset, size) combination. Is handled *exactly* the same way as as
/// UniformBuffer.
struct ShaderStorageBuffer : ResourceHandle {
    u32 _offset = 0;
    u32 _size = 0;

    ShaderStorageBuffer() = default;

    explicit ShaderStorageBuffer(GLuint handle, u32 offset, u32 size)
        : ResourceHandle(handle)
        , _offset(offset)
        , _size(size) {}

    u64 hash() const { return (u64(handle()) << 32) + _size + _offset; }

    // Returns an 'invalid' i.e. nil description. Used for the hash table.
    static ShaderStorageBuffer get_invalid() { return ShaderStorageBuffer(0, 0, 0); }
};

inline bool operator==(const ShaderStorageBuffer &b1, const ShaderStorageBuffer &b2) {
    return b1._handle == b2._handle && b1._offset == b2._offset && b1._size == b2._size;
}

} // namespace gl_desc

// Represents the format of a single vertex attribute. All non-instanced attributes are source from the 0-th
// input slot, aka the vertex buffer binding point.
struct VaoAttributeFormat {
    GLuint num_components;             // 1, 2, 3, or 4
    GLenum component_type;             // GL_FLOAT, GL_UNSIGNED_BYTE, GL_UNSIGNED_INT
    GLboolean normalize;               // Normalize when access from vertex shader?
    GLboolean using_integer_in_shader; // Using uint, uvec, etc. in shader?
    GLuint relative_offset;            // Relative offset in vertex data struct
    GLuint instances_before_advancing; // Instances to draw with before advancing to the next attrbute for
                                       // this binding point
    GLuint vbo_binding_point;          // Only relevant if instances_before_advancing is not 0

    using num_components_mask = IntMask<0, 3>;
    using component_type_mask = IntMask<3, 2>;
    using normalize_mask = IntMask<5, 1>;
    using using_integer_in_shader_mask = IntMask<6, 1>;
    using vbo_binding_point_mask = IntMask<7, 4>;
    using relative_offset_mask = IntMask<16, 16>;
    using instances_before_advancing_mask = IntMask<32, 32>;

    VaoAttributeFormat() = default;

    VaoAttributeFormat(GLuint num_components,
                       GLenum component_type,
                       GLboolean normalize,
                       GLint relative_offset,
                       GLuint instances_before_advancing = 0,
                       GLuint vbo_binding_point = 0,
                       GLboolean using_integer_in_shader = GL_FALSE)
        : num_components(num_components)
        , component_type(component_type)
        , normalize(normalize)
        , using_integer_in_shader(using_integer_in_shader)
        , relative_offset(relative_offset)
        , instances_before_advancing(instances_before_advancing)
        , vbo_binding_point(vbo_binding_point) {
        CHECK_LT_F(vbo_binding_point, 16u);
    }

    constexpr u64 get_compressed() const {
        u64 h = 0;
        h = num_components_mask::set(h, num_components);
        h = component_type_mask::set(
            h,
            component_type == GL_FLOAT
                ? 0
                : component_type == GL_UNSIGNED_BYTE ? 1 : component_type == GL_UNSIGNED_INT ? 2 : 3);
        h = normalize_mask::set(h, normalize == GL_TRUE ? 1 : 0);
        h = using_integer_in_shader_mask::set(h, using_integer_in_shader == GL_TRUE ? 1 : 0);
        h = vbo_binding_point_mask::set(h, vbo_binding_point);
        h = relative_offset_mask::set(h, relative_offset);
        h = instances_before_advancing_mask::set(h, instances_before_advancing);
        return h;
    }

    static constexpr VaoAttributeFormat uncompress(u64 h) {
        VaoAttributeFormat format = {};
        format.num_components = (u32)num_components_mask::extract(h);
        u32 component_type = (u32)component_type_mask::extract(h);
        format.component_type =
            component_type == 0 ? GL_FLOAT
                                : component_type == 1 ? GL_UNSIGNED_BYTE
                                                      : component_type == 2 ? GL_UNSIGNED_INT
                                                                            : GL_FLOAT; // Default is GL_FLOAT

        format.normalize = (u32)normalize_mask::extract(h) ? GL_TRUE : GL_FALSE;
        format.using_integer_in_shader = (u32)using_integer_in_shader_mask::extract(h) ? GL_TRUE : GL_FALSE;
        format.relative_offset = (u32)relative_offset_mask::extract(h);
        format.vbo_binding_point = (u32)vbo_binding_point_mask::extract(h);
        format.instances_before_advancing = (u32)instances_before_advancing_mask::extract(h);
        return format;
    }

    // Gives a desc that denotes an invalid attribute format. We maintain the invariant that its compressed
    // u32 version equals 0.
    static constexpr VaoAttributeFormat invalid() {
        VaoAttributeFormat format = {};
        format.component_type = GL_FLOAT;
        format.normalize = GL_FALSE;
        format.using_integer_in_shader = GL_FALSE;
        return format;
    }

    void set_format_for_gl(GLuint attrib_location) const {
        assert(IMPLIES(using_integer_in_shader, normalize == GL_FALSE));

        if (using_integer_in_shader == GL_FALSE) {
            glVertexAttribFormat(attrib_location, num_components, component_type, normalize, relative_offset);
        } else {
            glVertexAttribIFormat(attrib_location, num_components, component_type, relative_offset);
        }
    }
};

static_assert(VaoAttributeFormat::invalid().get_compressed() == 0u, "");

struct VaoFormatDesc {
    u64 compressed_attrib_formats[8]; // Yeah, max 8. Don't need more than that I suppose.

    static constexpr VaoFormatDesc invalid() {
        VaoFormatDesc desc = {};
        // 0 cannot be a valid format since num_components must be >= 1
        for (u32 i = 0; i < ARRAY_SIZE(desc.compressed_attrib_formats); ++i) {
            desc.compressed_attrib_formats[i] = 0;
        }
        return desc;
    }

    // Create a VaoFormatDesc from given attribute formats
    static VaoFormatDesc from_attribute_formats(const fo::Array<VaoAttributeFormat> &attribute_formats) {
        VaoFormatDesc desc = invalid();
        CHECK_LE_F(size(attribute_formats), ARRAY_SIZE(desc.compressed_attrib_formats));

        u32 i = 0;
        for (const VaoAttributeFormat &f : attribute_formats) {
            desc.compressed_attrib_formats[i++] = f.get_compressed();
        }
        return desc;
    }

    bool operator==(const VaoFormatDesc &other) const {
        for (u32 i = 0; i < ARRAY_SIZE(compressed_attrib_formats); ++i) {
            if (compressed_attrib_formats[i] != other.compressed_attrib_formats[i]) {
                return false;
            }
        }
        return true;
    }
};

struct PerCameraUBlockFormat {
    fo::Matrix4x4 view_from_world_xform;
    fo::Matrix4x4 clip_from_view_xform;
    fo::Vector4 eye_position;
};

struct BindingStateConfig {
    u32 expected_unique_vaos = 16;
    u32 per_camera_ubo_size = sizeof(PerCameraUBlockFormat);
    u32 expected_fbo_count = 4;
};

// Maximum number of attachments I allowed
constexpr i32 MAX_FBO_ATTACHMENTS = 4;

/// Checks if the framebuffer is complete. If incomplete, prints why.
bool framebuffer_complete(GLuint fbo);

// Convenience type for creating an offscreen framebuffer. Create and initialize with attachments once.
// Removing and setting new textures as attachments is not allowed once you are done creating the framebuffer.
struct FBO {
    GLuint _fbo_handle = 0;
    GLuint _color_buffer_textures[MAX_FBO_ATTACHMENTS]; // Color textures or renderbuffers
    GLuint _depth_texture = 0;                          // Depth, or depth+stencil texture or renderbuffers
    i8 _num_attachments = 0;                            // Number of color attachments
    const char *_name;                                  // Debug name

    bool has_depth_attachment() const;

    // Use this as an attachment number to `set_draw_buffers` to disable the fragment output.
    static constexpr i32 NO_ATTACHMENT = -1;

    // Returns the GL integer handle for this fbo.
    explicit operator GLuint() const { return _fbo_handle; }

    // Initialize an FBO object from a GLFW surface. This is a no-op and for readability only. You cannot
    // "read" from the textures (color/depth) attached to the on-screen framebuffer.
    FBO &init_from_default_framebuffer();

    FBO &gen(const char *name = "");

    FBO &bind();

    const FBO &bind() const;

    const FBO &bind_as_writable(GLuint readable_fbo = 0) const;

    const FBO &bind_as_readable(GLuint writable_fbo = 0) const;

    FBO &bind_as_writable(GLuint readable_fbo = 0);

    FBO &bind_as_readable(GLuint writable_fbo = 0);

    void _validate_attachment(i32 attachment_number);

    FBO &
    add_attachment(i32 attachment_number, GLuint texture, i32 level = 0, GLenum texture_type = GL_TEXTURE_2D);

    FBO &add_attachment_rbo(i32 attachment_number, GLuint rbo);

    // Add a texture as depth attachment
    FBO &add_depth_attachment(GLuint texture,
                              GLenum depth_attachment = GL_DEPTH_ATTACHMENT,
                              i32 level = 0,
                              GLenum texture_type = GL_TEXTURE_2D);

    // Add an rbo as depth attachment
    FBO &add_depth_attachment_rbo(GLuint rbo, GLenum depth_attachment = GL_DEPTH_ATTACHMENT);

    bool _is_done_creating() const;

    // Valid after creation complete only
    u32 num_attachments() const;

    // Set the draw buffers of this framebuffer. The i-th fragment shader output will be written to the
    // backing texture bound to attachment_of_output[i]. You can also give NO_ATTACHMENT as an attachment
    // number and the writes to that attachment will be disabled. Usually you will set it such that
    // attachment_of_output[i] = i or NO_ATTACHMENT. But you can use a single FBO to contain more than
    // one textures as attachment and render to them one after another while in the shader you just render to
    // output location 0. That is allowed just fine.
    FBO &set_draw_buffers(i32 *attachment_of_output, size_t count);

    FBO &set_read_buffer(i32 attachment_number);

    FBO &set_draw_buffers(std::initializer_list<i32> attachment_of_output) const {
        assert(attachment_of_output.size() <= num_attachments());
        std::array<i32, MAX_FBO_ATTACHMENTS> arr;
        std::fill(arr.begin(), arr.end(), NO_ATTACHMENT);
        i32 i = 0;
        for (auto a : attachment_of_output) {
            arr[i++] = a;
        }
        return const_cast<FBO *>(this)->set_draw_buffers(arr.data(), attachment_of_output.size());
    }

    FBO &set_read_buffer(i32 attachment_number) const {
        return const_cast<FBO *>(this)->set_read_buffer(attachment_number);
    }

    // Clear attached depth buffer (if any) with given value. Framebuffer should be currently bound as
    // writable.
    FBO &clear_depth(f32 d);

    FBO &clear_color(i32 attachment_number, fo::Vector4 color);

    // Call after you're done adding attachments
    FBO &set_done_creating();

    GLuint color_attachment_texture(int i = 0) const;
    GLuint depth_attachment_texture() const;
};

struct SetInputOutputFBO {
    FBO *input_fbo = nullptr;
    i32 read_attachment_number = FBO::NO_ATTACHMENT;
    FBO *output_fbo = nullptr;
    std::array<i32, MAX_FBO_ATTACHMENTS> attachment_of_output{};

    SetInputOutputFBO();

    SetInputOutputFBO &set_attachment_of_output(i32 output_number, i32 attachment_number) {
        attachment_of_output[output_number] = attachment_number;
        return *this;
    }

    void reset_struct();
};
// Set the input and output fbo along with the read and output attachment numbers for them respectively.
void set_input_output_fbos(SetInputOutputFBO &config);

namespace logl_internal {

template <typename ResourceDesc> struct PerResourceBindpoints {
    u32 _num_bindpoints_reserved;

    // Bitset indicating if a bindpoint is reserved
    fo::DyPackedUintArray<u32, GLuint> _bindpoint_is_reserved;

    // BindingPoint (GLuint) -> ResourceDesc.
    fo::Vector<ResourceDesc> _resource_at_point;

    ResourceDesc _invalid_desc;

    PerResourceBindpoints(fo::Allocator &allocator, u32 count, ResourceDesc invalid_desc)
        : _num_bindpoints_reserved(0)
        , _bindpoint_is_reserved(1, count, allocator)
        , _resource_at_point(allocator)
        , _invalid_desc(invalid_desc) {
        fo::resize(_resource_at_point, count);
        std::fill(fo::begin(_resource_at_point), fo::end(_resource_at_point), _invalid_desc);
    }

    // Reserve space for this many bindpoints.
    void reserve_space(u32 max_bindpoints) { fo::resize(_resource_at_point, max_bindpoints); }

    u32 max_bindpoints() const { return fo::size(_resource_at_point); }

    u32 space() const { return _num_bindpoints_reserved < max_bindpoints(); }

    // If resource is already bound, returns the binding point
    ::optional<u32> get_bound(const ResourceDesc &gl_desc) const;

    // Adds a new binding. If all binding points are already allocated, returns nullopt. Returns the binding
    // point number. For texture units, you do have to add GL_TEXTURE0 to the number.
    ::optional<GLuint> add_new(const ResourceDesc &gl_desc);

    void set(u32 bindpoint, const ResourceDesc &gl_desc);

    // Reserves one bindpoint, does not associate a valid resource description with it, yet.
    ::optional<GLuint> reserve_new();

    void release(const ResourceDesc &gl_desc);

    // Do something with each of the bindpoints via the given function. Function returns a bool that indicates
    // whether or not the iteration should continue. This function itself will finally return true if all
    // reserved bindpoints were traversed, otherwise will return false.
    bool iterate_by_function(std::function<bool(GLuint, const ResourceDesc &)>);
};

} // namespace logl_internal

void set_gl_depth_stencil_state(const DepthStencilStateDesc &ds);
void set_gl_rasterizer_state(const RasterizerStateDesc &rs);

inline void set_gl_blendfunc_state(const BlendFunctionDesc *desc_per_color_attachment, int num_attachments) {
    ABORT_F("Not implemented yet");
}

struct SavedBindingsHeader {
    // Total size of the block that this struct header precedes (excluding the size of this block)
    u32 data_block_size;
    u32 remaining_size;

    u32 rasterizer_state_number;
    u32 ds_state_number;

    // Where the binding and desc pairs begin for each bound resource. Contains both the offset and the count
    // as u16
    u16 offset_ubo_ranges;
    u16 num_ubo_ranges;

    u16 offset_ssbo_ranges;
    u16 num_ssbo_ranges;

    u16 offset_sampled_textures;
    u16 num_sampled_textures;

    alignas(8) u8 data[];
};

// Stores the state of all resource bindings (well you have to bind via this object)
struct BindingState : public NonCopyable {
    // Sets to an unitialized state
    BindingState();

    ~BindingState();

    void init(const BindingStateConfig &config);

    // Bind a texture to next available image unit. Returns the unit. Add GL_TEXTURE0 to it in order to bind
    // it yourself. See note below at end of file.
    GLuint bind_unique(const gl_desc::SampledTexture &texture);

    // Bind a uniform buffer range to next available uniform block binding point. Returns the binding point.
    GLuint bind_unique(const gl_desc::UniformBuffer &ubo);

    GLuint bind_unique(const gl_desc::ShaderStorageBuffer &ssbo);

    GLuint reserve_sampler_bindpoint();

    GLuint reserve_uniform_bindpoint();

    GLuint reserve_ssbo_bindpoint();

    // Release the binding point that is being used for given resource
    void release_bind(const gl_desc::SampledTexture &texture);

    // " " " "
    void release_bind(const gl_desc::UniformBuffer &ubo);

    // Return a sampler with the given attributes. Sampler objects are cached by this system and we should
    // obtain sampler objects via this method.
    GLuint get_sampler_object(const SamplerDesc &sampler_desc);

    // Add a rasterizer state description. Returns an index into the list of rasterizer states.
    u32 add_rasterizer_state(const RasterizerStateDesc &rs);

    // Sets the given rasterizer state as current
    void set_rasterizer_state(u32 rs_number);

    // Get a GL vao with the given formats.
    GLuint get_vao(const VaoFormatDesc &vao_format_desc);

    u32 add_depth_stencil_state(const DepthStencilStateDesc &ds);
    void set_depth_stencil_state(u32 ds_number);

    GLuint per_camera_ubo() const { return _per_camera_ubo.handle(); }
    GLuint per_camera_ubo_binding() const { return _per_camera_ubo_binding; }

    GLuint no_attrib_vao() const { return _no_attrib_vao; }

    void seal();

    void save_bindings(SavedBindingsHeader *saved, u32 bytes_available);

    // Restore bindings from previously SavedBindingsHeader.
    void restore_bindings(const SavedBindingsHeader *saved);

    void print_log() const;

    // Data

    // fo::TempAllocator4096 _temp_alloc;
    // fo::TempAllocator<clip_to_pow2(sizeof(SamplerDesc) + sizeof(GLuint)) * 4> _sampler_cache_alloc;

    // Allocators for everything.
    fo::ArenaAllocator _bs_stuff_alloc;
    fo::TempAllocator<clip_to_pow2(sizeof(SamplerDesc) + sizeof(GLuint)) * 4> _sampler_cache_alloc;

    bool _sealed;

    logl_internal::PerResourceBindpoints<gl_desc::SampledTexture> _textures_bind_info;
    logl_internal::PerResourceBindpoints<gl_desc::UniformBuffer> _uborange_bind_info;
    logl_internal::PerResourceBindpoints<gl_desc::ShaderStorageBuffer> _ssborange_bind_info;

    // Samplers don't have a fixed point. We can bind them to any binding point that has a texture bound to
    // it.
    fo::OrderedMap<SamplerDesc, GLuint> _sampler_cache;
    fo::Array<RasterizerStateDesc> _rasterizer_states;
    fo::Array<DepthStencilStateDesc> _depth_stencil_states;

    fo::Vector<FBO> _fbo_storage; // All FBOs allocated by the app
    fo::Vector<u32>
        _blend_factors_per_fbo; // Blend factor and op of FBOs are stored here sequentially. (Unused)

    // These two together form a mapping from vao format to generated vaos.
    struct VaoFormatAndObject {
        VaoFormatDesc format_desc;
        VertexArrayDeleter vao_deleter;
    };
    fo::Vector<VaoFormatAndObject> _vaos_generated;

    u32 _current_rasterizer_state;
    u32 _current_depth_stencil_state;

    gl_desc::UniformBuffer _per_camera_ubo = {};
    GLuint _per_camera_ubo_binding = 0;
    GLuint _no_attrib_vao = 0;

    GLuint pos_vao = 0;
    GLuint pos_normal_uv_vao = 0;
    GLuint pos2d_vao = 0;

    FBO _screen_fbo;
    FBO *_current_draw_fbo = nullptr;
    FBO *_current_read_fbo = nullptr;

    void set_blend_function(i32 output_number, const BlendFunctionDesc &blend_factor);
};

BindingState &default_binding_state();
void init_default_binding_state(const BindingStateConfig &config = BindingStateConfig());
void close_default_binding_state();

// ## Impl of templates

namespace logl_internal {

template <typename ResourceDesc>
::optional<u32> PerResourceBindpoints<ResourceDesc>::get_bound(const ResourceDesc &desc) const {
    for (u32 i = 0; i < fo::size(_resource_at_point); ++i) {
        if (_resource_at_point[i] == desc) {
            return i;
        }
    }
    return ::nullopt;
}

template <typename ResourceDesc>
::optional<GLuint> PerResourceBindpoints<ResourceDesc>::add_new(const ResourceDesc &desc) {
    for (u32 i = 0; i < fo::size(_resource_at_point); ++i) {
        if (!_bindpoint_is_reserved.get(i)) {
            _bindpoint_is_reserved.set(i, 1u);
            _resource_at_point[i] = desc;
            ++_num_bindpoints_reserved;
            return i;
        }
    }
    return ::nullopt;
}

template <typename ResourceDesc>
void PerResourceBindpoints<ResourceDesc>::set(u32 bindpoint, const ResourceDesc &desc) {
    DCHECK_LT_F(bindpoint, max_bindpoints());
    _bindpoint_is_reserved.set(bindpoint, 1u);
    _resource_at_point[bindpoint] = desc;
}

template <typename ResourceDesc>::optional<GLuint> PerResourceBindpoints<ResourceDesc>::reserve_new() {
    for (u32 i = 0; i < fo::size(_resource_at_point); ++i) {
        if (!_bindpoint_is_reserved.get(i)) {
            _bindpoint_is_reserved.set(i, 1u);
            ++_num_bindpoints_reserved;
            return i;
        }
    }
    return ::nullopt;
}

template <typename ResourceDesc> void PerResourceBindpoints<ResourceDesc>::release(const ResourceDesc &desc) {
    assert(_num_bindpoints_reserved > 0);
    for (u32 i = 0; i < fo::size(_resource_at_point); ++i) {
        if (_resource_at_point[i] == desc) {
            _resource_at_point[i] = _invalid_desc;
            --_num_bindpoints_reserved;
        }
    }
}

template <typename ResourceDesc>
bool PerResourceBindpoints<ResourceDesc>::iterate_by_function(
    std::function<bool(GLuint, const ResourceDesc &)> fn) {
    for (u32 bindpoint = 0; bindpoint < max_bindpoints(); ++bindpoint) {
        if (_bindpoint_is_reserved.get(bindpoint)) {
            bool ret = fn(bindpoint, _resource_at_point[bindpoint]);
            if (!ret) {
                DLOG_F(ERROR, "Could not iterate full list of bindpoints");
                return false;
            }
        }
    }
    return true;
}

} // namespace logl_internal

// -------- Reusing stuff from here for the new renderer interface.

GLuint make_vao(const VaoFormatDesc &vao_format);

} // namespace eng
