// A renderer abstraction that targets GL 4.5. Written in a C-ish style for the most part.

#pragma once

#include <learnogl/essential_headers.h>
#include <learnogl/type_utils.h>

#include <learnogl/constexpr_stuff.h>
#include <learnogl/fixed_string_buffer.h>
#include <learnogl/mesh.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <learnogl/uniform_structs.h>
#include <scaffold/arena_allocator.h>
#include <scaffold/bitflags.h>
#include <scaffold/open_hash.h>
#include <scaffold/pod_hash.h>
#include <scaffold/vector.h>

#include <functional>

#include <learnogl/gl_binding_state.h> // Taking some stuff from here for now.

// NOTE - Don't worry about hitting max resource limits in shaders. Have some default bindpoints for your use-
// case and rest you can assign semi-arbitrarily using bindpoints in glsl code. Read these bindpoints from
// GLSL, and bind resources before drawing.

namespace eng {

ENUMSTRUCT BindpointKind {
    enum E : u32 {
        UNIFORM_BUFFER_BINDING = 0,
        SHADER_STORAGE_BINDING,
        SAMPLED_TEXTURED_BINDING,
        IMAGE_BINDING,

        PIXEL_PACK_BUFFER_BINDING,
        PIXEL_UNPACK_BUFFER_BINDING,
        ARRAY_BUFFER_BINDING,
        ELEMENT_ARRAY_BUFFER_BINDING,

        COUNT
    };
};

ENUMSTRUCT GLObjectKind {
    enum E : u32 {
        NONE = 0,

        VERTEX_BUFFER,
        ELEMENT_ARRAY_BUFFER,
        PIXEL_PACK_BUFFER,
        PIXEL_UNPACK_BUFFER,
        UNIFORM_BUFFER,
        SHADER_STORAGE_BUFFER,
        ATOMIC_COUNTER_BUFFER,

        TEXTURE_1D,
        TEXTURE_2D,
        TEXTURE_3D,
        TEXTURE_CUBE,
        ARRAY_TEXTURE_1D,
        ARRAY_TEXTURE_2D,
        ARRAY_TEXTURE_3D,
        ARRAY_TEXTURE_CUBE,

        SAMPLER_OBJECT,

        VERTEX_SHADER,
        TESS_CONTROL_SHADER,
        TESS_EVAL_SHADER,
        GEOMETRY_SHADER,
        FRAGMENT_SHADER,
        COMPUTE_SHADER,

        GRAPHICS_PROGRAM,
        COMPUTE_PROGRAM,
        SYNC,
        FRAMEBUFFER,
        VAO,
        QUERY_OBJECT,
        SYNC_OBJECT,

        COUNT
    };

    static constexpr bool is_buffer(E e) { return VERTEX_BUFFER <= e && e <= PIXEL_UNPACK_BUFFER; }
    static constexpr bool is_shader_buffer(E e) { return UNIFORM_BUFFER <= e && e <= ATOMIC_COUNTER_BUFFER; }
    static constexpr bool is_shader(E e) { return VERTEX_SHADER <= e && e <= COMPUTE_SHADER; }
    static constexpr bool is_pixel_buffer(E e) { return PIXEL_PACK_BUFFER <= e && e <= PIXEL_UNPACK_BUFFER; }
    static constexpr bool is_texture(E e) { return TEXTURE_1D <= e && e <= ARRAY_TEXTURE_CUBE; }
};

constexpr u32 NUM_GL_BUFFER_KINDS = GLObjectKind::SHADER_STORAGE_BUFFER + 1;

using GLResource64 = u64; // 0 denotes invalid

// TODO: ^The main reason I even have this 'compressed' id is to be able to quickly extract the type and
// attributes of a resource and more importantly, the GLuint handle. It's really not worth it as I think of
// it. Hash lookups are not going to be that slow anyway. So I should remove maintaining these ids totally?
// Let's keep coding and see.

using RMResourceID16 = u16; // Internal to this system. 0 denotes invalid

// Bits denoting the actual GLuint handle
using GLResource64_GLuint_Mask = Mask64<0, 32>;
// Bits denoting the kind of the object
using GLResource64_Kind_Mask = Mask64<32, log2_ceil(GLObjectKind::COUNT)>;
// Bits denoting the render-manager specific id.
using GLResource64_RMID_Mask = Mask64<GLResource64_Kind_Mask::END,
                                      16>; // 2 ** 16 unique GL objects at any time

using GLFullPipelineStateId = u32;
using RasterizerStateIndex = u32;
using DepthStencilStateIndex = u32;
using BlendStateIndex = u32;

REALLY_INLINE RMResourceID16 resource_to_rmid(GLResource64 resource) {
    return (u16)GLResource64_RMID_Mask::extract(resource);
}

REALLY_INLINE GLObjectKind::E resource_kind_from_id16(RMResourceID16 resource) {
    return (GLObjectKind::E)GLResource64_Kind_Mask::extract(resource);
}

struct BufferCreateInfo;

struct BufferExtraInfo {
    u32 bytes;
};

struct Texture2DInfo {
    GLuint handle;
    u16 width, height, mips;
};

struct BindpointTypeState {
    fo::Vector<RMResourceID16> resource_at_bindpoint;

    u16 max_bindpoints;

    BindpointTypeState(u16 max_bindpoints, u32 expected_bindpoints = 16)
        : CTOR_INIT_FIELD(resource_at_bindpoint, 0, fo::memory_globals::default_allocator()) {
        fo::reserve(resource_at_bindpoint, expected_bindpoints);
    }
};

struct SingleBindpointTypeState {
    RMResourceID16 resource_at_bindpoint;
};

struct BindpointStates {
    BindpointTypeState uniform_buffer_bindpoints;
    BindpointTypeState storage_buffer_bindpoints;
    BindpointTypeState texture_bindpoints_state;

    // SingleBindpointTypeState vertex_buffer;
    SingleBindpointTypeState element_array_bindpoint;
    SingleBindpointTypeState vao_bindpoint;
    SingleBindpointTypeState pixel_pack_bindpoint;
    SingleBindpointTypeState pixel_unpack_bindpoint;
    SingleBindpointTypeState graphics_program_bindpoint;
    SingleBindpointTypeState compute_program_bindpoint;
};

struct ShaderInfo {
    GLuint handle;
    FixedStringBuffer::Index path_str_index;

    const char *pathname() const;
};

struct ShaderResourceInfo {
    enum AccessKind {
        READONLY,
        WRITEONLY,
        READWRITE,
    };

    enum ShaderResourceKind {
        UNIFORM_BUFFER,
        SHADER_STORAGE_BUFFER,
        COUNTER_BUFFER,
        IMAGE,
        SAMPLER,
    };

    enum SamplerDim {
        _1D,
        _2D,
        _3D,
        _CUBE,
    };

    enum SamplerBasicType {
        FLOAT,
        UINT,
        INT,
    };

    using access_kind_mask = Mask32<0, 2>;
    using resource_kind_mask = Mask32<2, 2>;
    using sampler_dim_mask = Mask32<4, 2>;
    using sampler_basic_mask = Mask32<6, 2>;

    u32 bits; // Above stuff denoting the type of the buffer/uniform variable

    u32 offset;    // Offset if it's a buffer resource
    u16 bindpoint; // The 'binding' value
};

ENUMSTRUCT BufferCreateBitflags {
    enum E : u32 {
        USE_BUFFER_STORAGE = 1,       // Use BufferStorage? Will use BufferData if not set
        SET_DYNAMIC_STORAGE = 1 << 1, // Will I use glBufferSubData frequently to update?
        SET_STATIC_STORAGE = 1 << 2,  // Will just init once during creation and that's it
        SET_CPU_WRITABLE = 1 << 3,    // Will to it after mapping glMapBuffer
        SET_CPU_READABLE = 1 << 4,    // Will read from it after glMapBuffer
        SET_PERSISTING = 1 << 5,      // Can keep mapped while GL uses the buffer at the same time
        SET_COHERENT = 1 << 6,        // Writes get visible to reads across cpu and gpu immediately
    };
};

using BufferCreateFlags = fo::BitFlags<BufferCreateBitflags::E>;

struct BufferCreateInfo {
    u32 bytes = 0;
    BufferCreateFlags flags;
    void *init_data = nullptr;
    const char *name = nullptr;
};

struct VertexBufferExtraInfo {
    u32 bytes = 0;
    BufferCreateFlags create_flags;

    // @rksht - Do I really need persistently mapped buffers now?
    void *persisting_ptr = nullptr;
};

struct IndexBufferExtraInfo {
    u32 bytes = 0;
    BufferCreateFlags create_flags;
};

/// Enum denoting the data types returned when a sampler is used to sample from a texture in shader. This is
/// only used for validating purposes.
ENUMSTRUCT TexelSamplerScalarType {
    enum E : u32 { INVALID = 0, FLOAT, UNSIGNED_INT, SIGNED_INT, COUNT };

    DECL_BITS_CHECK(u32);
};

// What the components are in the client texture (format needed to be known before allocating storage or after
// packing them to the client side). There are way too many formats. I'm representing only the common ones I
// can foresee being used by me.
ENUMSTRUCT TexelBaseType {
    enum E : u32 { INVALID = 0, FLOAT, FLOAT16, U8, U16, U32, S8, S16, S32, DEPTH32, COUNT };

    static constexpr bool is_signed(E e) { return S8 <= e && e <= S32; }
    static constexpr bool is_unsigned(E e) { return U8 <= e && e <= U32; }
    static constexpr bool is_integer(E e) { return is_signed(e) || is_unsigned(e); }
    static constexpr bool is_float(E e) { return e == FLOAT; }

    DECL_BITS_CHECK(u32);

    ENUM_MASK32(0);
};

ENUMSTRUCT TexelComponents {
    enum E : u32 { INVALID = 0, R, RG, RGB, RGBA, DEPTH, DEPTH32F_STENCIL8, DEPTH24_STENCIL8, COUNT };
    // ^ Depth components are only valid if you're specifying an internal format

    DECL_BITS_CHECK(u32);

    ENUM_MASK32(TexelBaseType::end_bit);
};

ENUMSTRUCT TexelInterpretType {
    enum E : u32 { INVALID = 0, NORMALIZED, UNNORMALIZED, COUNT };

    DECL_BITS_CHECK(u32);
    ENUM_MASK32(TexelComponents::end_bit);
};

/// Represents complete usage of each texel. `client_type` denotes how the texels are packed in the client
/// (C++) side and together with `interpret_type` denotes how a shader is going to obtain the value. Like U8
/// and NORMALIZED will yield floats in shader. `components` is a semantic that denotes usual components like
/// R, G, B, A, or DEPTH.
struct TexelInfo {
    TexelBaseType::E internal_type;
    TexelComponents::E components;
    TexelInterpretType::E interpret_type;
};

constexpr int num_channels_for_components(TexelComponents::E components) {
    switch (components) {
    case TexelComponents::R:
        return 1;
    case TexelComponents::RG:
        return 2;
    case TexelComponents::RGB:
        return 3;
    case TexelComponents::RGBA:
        return 4;
    default:
        return 0;
    }
}

/// TexelInfo packed into a u32
using TexelInfo32 = u32;

inline constexpr TexelInfo32 _ENCODE_TEXEL_INFO(TexelBaseType::E client_type,
                                                TexelComponents::E components,
                                                TexelInterpretType::E interpret_type) {
    TexelInfo32 bits = TexelBaseType::mask::shift(client_type) | TexelComponents::mask::shift(components) |
                       TexelInterpretType::mask::shift(interpret_type);
    return bits;
}

inline constexpr TexelInfo _DECODE_TEXEL_INFO(TexelInfo32 bits) {
    TexelInfo info = {};
    info.internal_type = (TexelBaseType::E)TexelBaseType::mask::extract(bits);
    info.components = (TexelComponents::E)TexelComponents::mask::extract(bits);
    info.interpret_type = (TexelInterpretType::E)TexelInterpretType::mask::extract(bits);
    return info;
}

struct GLExternalFormat {
    GLenum components;     // 'format' argument to TexSubImage
    GLenum component_type; // 'type' argument to TexSubImage

    GLExternalFormat() = default;

    constexpr GLExternalFormat(GLenum components, GLenum component_type)
        : components(components)
        , component_type(component_type) {}

    constexpr bool invalid() const { return component_type == GL_NONE || components == GL_NONE; }
};

struct GLInternalFormat {
    GLenum e = GL_NONE;
};

struct TextureCreateInfo {
    // Keep this 0 if source is a file, then width, height and mips will be read from given file.
    u16 width = 0;
    u16 height = 0;
    u16 depth = 0;
    u8 mips = 1;

    // Information regarding type of values texels read/written by shaders and creation/sourcing by client
    // side.
    TexelInfo texel_info = {};

    // If given storage_texture is valid, creates a texture view with the storage_texture as the backing
    // texture. (TODO)
    RMResourceID16 storage_texture = 0;

    // Source can be `nullptr`, and in that case only storage will be allocated. path is here for loading
    // from a file. But probably won't implement that here.
    ::VariantTable<RMResourceID16, fs::path, u8 *> source = (u8 *)nullptr;
};

struct TextureInfo {
    RMResourceID16 rmid = 0;
    GLObjectKind::E texture_kind;

    u16 width = 0;
    u16 height = 0;
    u16 depth = 0;
    u16 layers = 0; // If this is an array texture, layers >= 1.
    u8 mips = 0;

    TexelSamplerScalarType::E sampler_type;
    TexelBaseType::E internal_type;

    // If this texture is actually a 'view texture', the original texture ('storage' texture) is pointed
    // to by this id. (TODO)
    RMResourceID16 storage_texture = 0;
};

static constexpr uint MAX_FRAMEBUFFER_COLOR_TEXTURES = 8;

// Use fmt to place a name field
constexpr const char *camera_transform_ublock_str = R"(
uniform {} {{
    mat4 view;
    mat4 proj;
    vec4 camera_pos;
    vec4 camera_quat;
}}
)";

struct ShaderProgramKey {
    uint32_t k0, k1, k2;

    bool operator==(const ShaderProgramKey &o) const { return k0 == o.k0 && k1 == o.k1 && k2 == o.k2; }
    uint64_t hash() const noexcept { return k0 + uint64_t(~uint32_t(0)) - (k1 ^ k2); };
};

static constexpr u32 CAMERA_TRANSFORM_UBLOCK_SIZE = sizeof(CameraTransformUB);

/// Where some shader string can be stored before passing to create_shader_object`. Usually a file.
using ShaderSourceType = VariantTable<const char *, fs::path>;

struct GLObjectHandle {
    RMResourceID16 _rmid = 0;
    RMResourceID16 rmid() const { return _rmid; }
};

struct VertexBufferHandle : GLObjectHandle {
    VertexBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct RenderManager; // Fwd

struct IndexBufferHandle : GLObjectHandle {
    IndexBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct UniformBufferHandle : GLObjectHandle {
    UniformBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct PixelPackBufferHandle : GLObjectHandle {
    PixelPackBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct PixelUnpackBufferHandle : GLObjectHandle {
    PixelUnpackBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct ShaderStorageBufferHandle : GLObjectHandle {
    ShaderStorageBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct Texture2DHandle : GLObjectHandle {
    Texture2DHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct Texture3DHandle : GLObjectHandle {
    Texture3DHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct VertexShaderHandle : GLObjectHandle {
    VertexShaderHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct FragmentShaderHandle : GLObjectHandle {
    FragmentShaderHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct GeometryShaderHandle : GLObjectHandle {
    GeometryShaderHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct TessControlShaderHandle : GLObjectHandle {
    TessControlShaderHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct TessEvalShaderHandle : GLObjectHandle {
    TessEvalShaderHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct ComputeShaderHandle : GLObjectHandle {
    ComputeShaderHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct ShaderProgramHandle : GLObjectHandle {
    ShaderProgramHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct VertexArrayHandle : GLObjectHandle {
    VertexArrayHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

struct SamplerObjectHandle : GLObjectHandle {
    SamplerObjectHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

using VarShaderHandle = ::VariantTable<VertexShaderHandle,
                                       FragmentShaderHandle,
                                       GeometryShaderHandle,
                                       TessControlShaderHandle,
                                       TessEvalShaderHandle,
                                       ComputeShaderHandle>;

// Max fragment output locations will obviously be <= MAX_FBO_ATTACHMENTS
constexpr u32 MAX_FRAGMENT_OUTPUTS = MAX_FBO_ATTACHMENTS;

// Some useful per FBO info for our purposes will be to know the data type and dimension of each
// attachment.
struct FboPerAttachmentDim {
    struct AttachmentInfo {
        u16 width = 0;
        u16 height = 0;
        TexelSamplerScalarType::E sampler_type = TexelSamplerScalarType::INVALID;
        TexelBaseType::E internal_type = TexelBaseType::INVALID;

        // Is valid if a texture is backing this attachment
        bool valid() const { return sampler_type != TexelSamplerScalarType::INVALID; }
    };

    std::array<AttachmentInfo, MAX_FRAGMENT_OUTPUTS> color_attachment_dims;
    AttachmentInfo depth_attachment_dim;

    bool operator==(const FboPerAttachmentDim &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

// Users should create the attachment index from attachment via these functions.
inline i32 depth_attachment() { return -1; }
inline i32 stencil_attacgment() { return -2; }
inline i32 color_attachment(i32 color_attachment_number) { return color_attachment_number; }

// Denotes an attachment and the value it will be cleared with when bound as a destination fbo.
struct AttachmentAndClearValue {
    i32 attachment;
    fo::Vector4 clear_value;

    AttachmentAndClearValue() = default;

    AttachmentAndClearValue(i32 attachment, const fo::Vector4 &clear_value)
        : attachment(attachment)
        , clear_value(clear_value) {}

    static i32 depth_attachment() { return -1; }

    static i32 color_attachment(i32 color_attachment_number) { return color_attachment_number; }
};

struct NewFBO : Str {
    FboPerAttachmentDim _dims;

    std::array<GLResource64, MAX_FRAGMENT_OUTPUTS> _color_textures = {};
    GLResource64 _depth_texture = 0;
    GLResource64 _fbo_id64 = 0;

    // Set this to the clear color you want
    fo::Vector4 clear_color = eng::math::zero_4;
    f32 clear_depth = -1.0f;

    StaticVector<AttachmentAndClearValue, MAX_FRAGMENT_OUTPUTS + 1> _attachments_to_clear = {};

    // Just an internal array of GL draw-buffers numbers used by bind_destination_fbo.
    std::array<GLenum, MAX_FRAGMENT_OUTPUTS> _gl_draw_buffers;

    // Internal boolean. True if this denotes default framebuffer.
    bool _is_default_fbo = false;

    const char *_debug_label = "";

    // Set given `attachment` to be cleared after being bound as a destination fbo. If attachment is -1,
    // it denotes the depth attachment. Otherwise, it should be positive and denote a color attachment.
    NewFBO &clear_attachment_after_bind(i32 attachment, const fo::Vector4 &value = eng::math::zero_4);

    u32 num_color_textures() const {
        if (_fbo_id64 == 0) {
            // This represents the default framebuffer
            return 1;
        }

        u32 count = 0;
        for (auto id64 : _color_textures) {
            count += u32(GLResource64_RMID_Mask::extract(id64) != 0);
        }
        return count;
    }

    bool has_depth_texture() const { return GLResource64_RMID_Mask::extract(_depth_texture) != 0; }

    virtual const char *str() const { return _debug_label; }
};

struct RenderManager : NonCopyable {
    FixedStringBuffer _debug_labels_buffer;

    fo::ArenaAllocator _allocator;

    fo::TempAllocator512 _small_allocator{ fo::memory_globals::default_allocator() };

    // Camera uniform buffer for common use.
    GETONLY(UniformBufferHandle, camera_ubo_handle) = 0;

    // Buffer hash maps

    fo::PodHash<RMResourceID16, BufferExtraInfo> vertex_buffers =
        fo::make_pod_hash<RMResourceID16, BufferExtraInfo>(_allocator);

    fo::PodHash<RMResourceID16, BufferExtraInfo> element_array_buffers =
        fo::make_pod_hash<RMResourceID16, BufferExtraInfo>(_allocator);

    fo::PodHash<RMResourceID16, BufferExtraInfo> uniform_buffers =
        fo::make_pod_hash<RMResourceID16, BufferExtraInfo>(_allocator);
    fo::PodHash<RMResourceID16, BufferExtraInfo> storage_buffers =
        fo::make_pod_hash<RMResourceID16, BufferExtraInfo>(_allocator);
    fo::PodHash<RMResourceID16, BufferExtraInfo> pixel_pack_buffers =
        fo::make_pod_hash<RMResourceID16, BufferExtraInfo>(_allocator);

    fo::PodHash<RMResourceID16, BufferExtraInfo> pixel_unpack_buffers =
        fo::make_pod_hash<RMResourceID16, BufferExtraInfo>(_allocator);

    // Array map of resource id to the above buffer tables keyed by type.
    std::array<fo::PodHash<RMResourceID16, BufferExtraInfo> *, NUM_GL_BUFFER_KINDS> _kind_to_buffer;

    fo::PodHash<RMResourceID16, u32> _buffer_sizes = fo::make_pod_hash<RMResourceID16, u32>(_allocator);

    fo::PodHash<RMResourceID16, TextureInfo> texture_infos =
        fo::make_pod_hash<RMResourceID16, TextureInfo>(_allocator);

    // Map from rmid to shader objects
    fo::PodHash<RMResourceID16, ShaderInfo> _shaders =
        fo::make_pod_hash<RMResourceID16, ShaderInfo>(_allocator);

    fo::PodHash<RMResourceID16, int> resource_to_name_map =
        fo::make_pod_hash<RMResourceID16, int>(_allocator);

    fo::OrderedMap<SamplerDesc, RMResourceID16> sampler_cache{ _allocator };
    fo::Array<RasterizerStateDesc> _cached_rasterizer_states{ _allocator };
    fo::Array<DepthStencilStateDesc> _cached_depth_stencil_states{ _allocator };
    fo::Array<BlendFunctionDesc> _cached_blendfunc_states{ _allocator };

    // All the currently allocated FBOs.
    fo::Vector<NewFBO> _fbos{ _allocator };

    // Storing linked shaders as a GLResource64. Can get the rmid if we want. User-side doesn't need it.
    fo::PodHash<ShaderProgramKey, GLResource64, decltype(&ShaderProgramKey::hash)> _linked_shaders =
        fo::make_pod_hash<ShaderProgramKey, GLResource64>(fo::memory_globals::default_allocator(),
                                                          &ShaderProgramKey::hash);

    fo::PodHash<RMResourceID16, ShaderResourceInfo> shader_resources =
        fo::make_pod_hash<RMResourceID16, ShaderResourceInfo>(_allocator);

    // Map from rmid16 to GLuint handles
    fo::PodHash<RMResourceID16, GLResource64> _rmid16_to_res64 =
        fo::make_pod_hash<RMResourceID16, GLResource64>(_allocator);

    DEFINE_TRIVIAL_PAIR(VaoFormatAndRMID, VaoFormatDesc, format_desc, RMResourceID16, rmid);

    fo::Vector<VaoFormatAndRMID> _vaos_generated{ 0, _allocator };

    // Constants that don't change after assigned. For now, these are hardcoded to
    // minimum defaults.

    RMResourceID16 _num_allocated_ids = 0;
    fo::Array<RMResourceID16> _deleted_ids{ _small_allocator }; // IDs of resources that are deleted

    i16 _num_view_ids = 0;

    fo::Array<mesh::StrippedMeshData> _stripped_mesh_datas;

    // Path to shader files are kept in a fixed string buffer. Shaders are operated on individually, and a
    // linked shader program is created only upon seeing if there aren't currently a program already
    // linked comprising of the given per-stage shaders.
    FixedStringBuffer _shader_paths;
    fo::OrderedMap<fo_ss::Buffer, RMResourceID16> _path_to_shader_rmid;

    FBO _screen_fbo;

    VertexArrayHandle pos2d_vao = 0;
    VertexArrayHandle pos_vao = 0;
    VertexArrayHandle pn_vao = 0;
    VertexArrayHandle pnu_vao = 0;
    VertexArrayHandle pnut_vao = 0;

    RenderManager(fo::Allocator &backing_allocator = fo::memory_globals::default_allocator());
};

inline GLuint get_gluint_from_rmid(RenderManager &self, RMResourceID16 rmid16) {
    auto lookup = find_with_end(self._rmid16_to_res64, rmid16);
    DCHECK_F(lookup.found());
    return GLResource64_GLuint_Mask::extract(lookup.keyvalue().second());
}

struct GL_ShaderResourceBinding {
    GLuint bindpoint;
    GLuint offset;
    u32 bytes;
    GLResource64 resource;
};

struct FboId {
    u16 _id;

    // Always use this to get the default fbo's id. Don't make an FboId{0} yourself.
    static inline FboId for_default_fbo() { return FboId{ 0 }; }
};

// Create a new fbo with given backing textures
FboId create_fbo(RenderManager &self,
                 const fo::Array<RMResourceID16> &color_textures,
                 RMResourceID16 depth_texture,
                 const fo::Array<AttachmentAndClearValue> &clear_attachments,
                 const char *debug_label = nullptr);

FboId create_default_fbo(RenderManager &self, const fo::Array<AttachmentAndClearValue> &clear_attachments);

// Bind the given fbo as the destination of fragment outputs. Specify the attachment map such that the
// fragment shader output to location i goes to the backing texture at attachment_map[i]
void bind_destination_fbo(RenderManager &rm,
                          FboId fbo_id,
                          const ::StaticVector<i32, MAX_FBO_ATTACHMENTS> &attachment_map);

// Bind the given fbo as the source of pixel reads, framebuffer blits, etc. The attachment_number denotes
// which color attachment will be used as the source in subsequent glReadPixels call. If you are only
// going to read data from shader, you don't need to specify any attachment number and passing -1 denotes
// that case.
void bind_source_fbo(eng::RenderManager &rm, FboId fbo_id, i32 attachment_number = -1);

void init_render_manager(RenderManager &self);

void shutdown_render_manager(RenderManager &self);

// -- These are simple ids for the stored render states.

struct RasterizerStateId {
    u16 _id;

    operator bool() const { return bool(_id); }
};

struct DepthStencilStateId {
    u16 _id;

    operator bool() const { return bool(_id); }
};

struct BlendFunctionDescId {
    u16 _id;

    operator bool() const { return bool(_id); }
};

// -- Buffer creation functions

UniformBufferHandle create_uniform_buffer(RenderManager &self, const BufferCreateInfo &ci);
ShaderStorageBufferHandle create_storage_buffer(RenderManager &self, const BufferCreateInfo &ci);
VertexBufferHandle create_vertex_buffer(RenderManager &self, const BufferCreateInfo &ci);
IndexBufferHandle create_element_array_buffer(RenderManager &self, const BufferCreateInfo &ci);
PixelPackBufferHandle create_pixel_pack_buffer(RenderManager &self, const BufferCreateInfo &ci);
PixelUnpackBufferHandle create_pixel_unpack_buffer(RenderManager &self, const BufferCreateInfo &ci);

// -- Texture creation function

Texture2DHandle
create_texture_2d(RenderManager &self, TextureCreateInfo texture_ci, const char *name = nullptr);

Texture3DHandle
create_texture_3d(RenderManager &self, TextureCreateInfo &texture_ci, const char *name = nullptr);

struct ShaderDefines; // Fwd

// Being compatible for now.
enum ShaderKind {
    UNSPECIFIED_SHADER_KIND, // Will use the filename to identity
    VERTEX_SHADER = GLObjectKind::VERTEX_SHADER,
    GEOMETRY_SHADER = GLObjectKind::GEOMETRY_SHADER,
    TESS_CONTROL_SHADER = GLObjectKind::TESS_CONTROL_SHADER,
    TESS_EVAL_SHADER = GLObjectKind::TESS_EVAL_SHADER,
    FRAGMENT_SHADER = GLObjectKind::FRAGMENT_SHADER,
    COMPUTE_SHADER = GLObjectKind::COMPUTE_SHADER,
};

VarShaderHandle create_shader_object(RenderManager &self,
                                     const fs::path &shader_source_file,
                                     ShaderKind shader_kind,
                                     const ShaderDefines &macro_defs,
                                     const char *debug_label = nullptr);

// Compiles a vertex shader from given source file. If the extension is ".vert" then simply passes to
// create_shader_object, otherwise defines a macro `DO_VERTEX_SHADER` to 1 and then compiles it.
VertexShaderHandle create_vertex_shader(RenderManager &self,
                                        const fs::path &shader_source_file,
                                        ShaderDefines &macro_defs,
                                        const char *debug_label = nullptr);

// Similar as above, but for fragment shader with DO_FRAGMENT_SHADER flag defined
FragmentShaderHandle create_fragment_shader(RenderManager &self,
                                            const fs::path &shader_source_file,
                                            ShaderDefines &macro_defs,
                                            const char *debug_label = nullptr);
// The flag defined is DO_TESS_CONTROL_SHADER
TessControlShaderHandle create_tess_control_shader(RenderManager &self,
                                                   const fs::path &shader_source_file,
                                                   ShaderDefines &macro_defs,
                                                   const char *debug_label = nullptr);

// The flag defined is DO_TESS_EVAL_SHADER
TessEvalShaderHandle create_tess_eval_shader(RenderManager &self,
                                             const fs::path &shader_source_file,
                                             ShaderDefines &macro_defs,
                                             const char *debug_label = nullptr);

// The flag defined is DO_GEOMETRY_SHADER
GeometryShaderHandle create_geometry_shader(RenderManager &self,
                                            const fs::path &shader_source_file,
                                            ShaderDefines &macro_defs,
                                            const char *debug_label = nullptr);

ComputeShaderHandle create_compute_shader(RenderManager &self,
                                          const fs::path &shader_source_file,
                                          ShaderDefines &macro_defs,
                                          const char *debug_label = nullptr);

// Create a sampler object
SamplerObjectHandle create_sampler_object(RenderManager &self, const SamplerDesc &desc);

struct ShadersToUse {
    RMResourceID16 vs = 0;
    RMResourceID16 tc = 0;
    RMResourceID16 te = 0;
    RMResourceID16 gs = 0;
    RMResourceID16 fs = 0;
    RMResourceID16 cs = 0;

    ShadersToUse() = default;

    // Call this if you want to re-use this struct for specifying another set of shaders.
    void reset() { *this = {}; }

    bool is_empty() const { return vs == tc && tc == te && te == gs && gs == fs && fs == cs && cs == 0; }

    // Not important for user side. Converts to a key suitable for the hash-map
    ShaderProgramKey key() const {
        return { (uint32_t(vs) << 16) | uint32_t(fs),
                 (uint32_t(tc) << 16) | uint32_t(te),
                 (uint32_t(gs) << 16) | uint32_t(cs) };
    }

    static constexpr ShadersToUse from_vs_fs(RMResourceID16 vs, RMResourceID16 fs) {
        ShadersToUse use;
        use.vs = vs;
        use.fs = fs;
        return use;
    }

    // Returns a string containing the paths of the shaders this program was linked from.
    fo_ss::Buffer source_paths_as_string(const RenderManager &rm) const;
};

const ShadersToUse get_shaders_used_by_program(const RenderManager &self, const ShaderProgramHandle &program);

// Set the shaders to the pipeline
RMResourceID16
set_shaders(RenderManager &rm, const ShadersToUse &shaders_to_use, const char *debug_label = nullptr);

// Creates and links program if it hasn't already been linked. Returns its id.
RMResourceID16
link_shader_program(RenderManager &rm, const ShadersToUse &shaders_to_use, const char *debug_label = nullptr);

void set_program(RenderManager &self, const ShaderProgramHandle handle);

constexpr u32 enumerated_shader_kind(ShaderKind shader_kind) {
    if (shader_kind == UNSPECIFIED_SHADER_KIND) {
        return 0;
    }
    return shader_kind - ShaderKind::VERTEX_SHADER + 1;
}

/// Create a shader object from the given sources. Defined in shader.cpp.
GLuint create_shader_object(ShaderSourceType shader_source,
                            ShaderKind shader_kind,
                            const ShaderDefines &macro_defs,
                            const char *debug_label = nullptr);

VertexArrayHandle create_vao(RenderManager &self, const VaoFormatDesc &ci, const char *debug_label = nullptr);

RasterizerStateId create_rs_state(RenderManager &self, const RasterizerStateDesc &desc);
DepthStencilStateId create_ds_state(RenderManager &self, const DepthStencilStateDesc &desc);
BlendFunctionDescId create_blendfunc_state(RenderManager &self, const BlendFunctionDesc &desc);

inline void clear_screen_fbo_color(RenderManager &self, const fo::Vector4 &color) {
    self._screen_fbo.clear_color(0, color);
}

inline void clear_screen_fbo_depth(RenderManager &self, float depth) { self._screen_fbo.clear_depth(depth); }

// Bind the resource to given bindpoint
void bind_to_bindpoint(RenderManager &self, UniformBufferHandle ubo_handle, GLuint bindpoint);

void add_backing_texture_to_view(RenderManager &self, RMResourceID16 view_fb_rmid);
void add_backing_depth_texture_to_view(RenderManager &self);

struct SourceToBufferExtraInfo {
    const void *source_bytes;
    u32 num_bytes;
    u32 byte_offset;
    bool discard; // Think D3D's "MAP_DISCARD"

    static constexpr u32 COPY_FULL = 0;

    // Makes an info which tells source_to_***_buffer to discard the currently stored data.
    static constexpr SourceToBufferExtraInfo
    after_discard(const void *source_bytes, u32 byte_offset, u32 num_bytes = COPY_FULL) {
        SourceToBufferExtraInfo info = {};
        info.source_bytes = source_bytes;
        info.num_bytes = num_bytes;
        info.byte_offset = byte_offset;
        info.discard = true;

        return info;
    }

    // Opposite of above.
    static constexpr SourceToBufferExtraInfo
    dont_discard(const void *source_bytes, u32 byte_offset, u32 num_bytes = COPY_FULL) {
        SourceToBufferExtraInfo info = {};
        info.source_bytes = source_bytes;
        info.num_bytes = num_bytes;
        info.byte_offset = byte_offset;
        info.discard = false;

        return info;
    }
};

void source_to_uniform_buffer(RenderManager &self,
                              UniformBufferHandle ubo,
                              SourceToBufferExtraInfo source_info);

struct PixelBufferCopyInfo {
    RMResourceID16 rmid = 0;
    u32 byte_offset = 0;
};

using MemoryOrPBOInfo = VariantTable<RMResourceID16, void *>;

// Command to copy data from/to buffer, texture, attachment to a pixel buffer or
// raw memory
void copy_texture_level_to_memory(RMResourceID16 rmid, u16 level, MemoryOrPBOInfo dest);

struct TextureLevelCopyRange {
    int x = 0;
    int y = 0;
    int dest_x = 0;
    int dest_y = 0;
    int width;
    int height;
    int source_level = 0;
    int dest_level = 0;
};

// This copies the level of one texture to the level of another texture. This is so named because you
// can't go directly copy texture levels or even full textures in opengl without first creating a
// framebuffer and setting the source level as the GL_READ_TARGET. Strange. I will keep a "dummy" FBO for
// this purpose.
void copy_texture_level_to_texture_level_2D(RMResourceID16 source_texture,
                                            RMResourceID16 dest_texture,
                                            const TextureLevelCopyRange &range);

struct CopyBufferRange {
    u32 read_offset = 0;
    u32 write_offset = 0;
    u32 size = 0;
};

void copy_buffer_to_buffer(RMResourceID16 source_buffer,
                           RMResourceID16 dest_buffer,
                           const CopyBufferRange &range);

// Set depth-stencil state
void set_ds_state(RenderManager &rm, DepthStencilStateId state_id);

// Set rasterizer state
void set_rs_state(RenderManager &rm, RasterizerStateId state_id);

// Set blend-function state
void set_blendfunc_state(RenderManager &rm, i32 output_number, BlendFunctionDescId &state_id);

#if 0
// Renderable objects representation
struct RenderableObject {
    Renderpass *render_pass = nullptr;
    u32 render_list_index = 0;

    virtual ~RenderableObject() { render_list_index = 0; }

    void set_render_list_index(u32 i) { render_list_index = i; }
};

struct VBO_EBO_RenderableObject : RenderableObject {
    RMResourceID16 vbo_id;
    RMResourceID16 ebo_id;
    RMResourceID16 vao_id;
    u32 vbo_offset;
    u32 ebo_offset;

    fo::Matrix4x4 world_f_local = eng::math::identity_matrix;
};

#endif

// Miscellaneous pure helpers

// Create a VAO format description from the given mesh data.
VaoFormatDesc vao_format_from_mesh_data(const mesh::StrippedMeshData &m);

} // namespace eng
