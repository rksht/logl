// A renderer abstraction that targets GL 4.5. Written in a C-ish style for the most part.

#pragma once

#include <learnogl/essential_headers.h>
#include <learnogl/type_utils.h>

#include <learnogl/constexpr_stuff.h>
#include <learnogl/fixed_string_buffer.h>
#include <learnogl/mesh.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <scaffold/arena_allocator.h>
#include <scaffold/bitflags.h>
#include <scaffold/open_hash.h>
#include <scaffold/pod_hash.h>
#include <scaffold/vector.h>

#include <functional>

#include <learnogl/gl_binding_state.h> // Taking some stuff from here for now.

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

using GLFullPipelineStateID = u32;
using RasterizerStateIndex = u32;
using DepthStencilStateIndex = u32;
using BlendStateIndex = u32;

REALLY_INLINE RMResourceID16 resource_to_rmid(GLResource64 resource) {
    return (u16)GLResource64_RMID_Mask::extract(resource);
}

REALLY_INLINE GLObjectKind::E resource_kind_from_id16(RMResourceID16 resource) {
    return (GLObjectKind::E)GLResource64_Kind_Mask::extract(resource);
}

struct BufferInfo {
    GLuint handle;
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

struct VertexBufferInfo {
    u32 bytes = 0;
    BufferCreateFlags create_flags;

    // @rksht - Do I really need persistently mapped buffers now?
    void *persisting_ptr = nullptr;
};

struct IndexBufferInfo {
    u32 bytes = 0;
    BufferCreateFlags create_flags;
};

// How a texel is going to be read from the shader.
ENUMSTRUCT TexelFetchType {
    enum E : u32 { INVALID = 0, FLOAT, UNSIGNED_INT, SIGNED_INT, COUNT };

    DECL_BITS_CHECK(u32);
};

// What the components are in the client texture (before sourcing, or after
// packing them to the client side)
ENUMSTRUCT TexelOrigType {
    enum E : u32 { INVALID = 0, FLOAT, U8, U16, U32, S8, S16, S32, DEPTH32, COUNT };

    static constexpr bool is_signed(E e) { return S8 <= e && e <= S32; }
    static constexpr bool is_unsigned(E e) { return U8 <= e && e <= U32; }
    static constexpr bool is_integer(E e) { return is_signed(e) || is_unsigned(e); }
    static constexpr bool is_float(E e) { return e == FLOAT; }

    DECL_BITS_CHECK(u32);

    ENUM_MASK32(0);
};

ENUMSTRUCT TexelComponents {
    enum E : u32 { INVALID = 0, R, RG, RGB, RGBA, DEPTH, COUNT };

    DECL_BITS_CHECK(u32);

    ENUM_MASK32(TexelOrigType::end_bit);
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
    TexelOrigType::E client_type;
    TexelComponents::E components;
    TexelInterpretType::E interpret_type;
};

/// TexelInfo packed into a u32
using TexelInfo32 = u32;

inline constexpr TexelInfo32 _ENCODE_TEXEL_INFO(TexelOrigType::E client_type,
                                                TexelComponents::E components,
                                                TexelInterpretType::E interpret_type) {
    TexelInfo32 bits = TexelOrigType::mask::shift(client_type) | TexelComponents::mask::shift(components) |
                       TexelInterpretType::mask::shift(interpret_type);
    return bits;
}

inline constexpr TexelInfo _DECODE_TEXEL_INFO(TexelInfo32 bits) {
    TexelInfo info = {};
    info.client_type = (TexelOrigType::E)TexelOrigType::mask::extract(bits);
    info.components = (TexelComponents::E)TexelComponents::mask::extract(bits);
    info.interpret_type = (TexelInterpretType::E)TexelInterpretType::mask::extract(bits);
    return info;
}

struct GLExternalFormat {
    GLenum component_type;
    GLenum components;

    constexpr bool invalid() { return component_type != GL_NONE && components != GL_NONE; }
};

struct TextureCreateInfo {
    u16 width = 0;
    u16 height = 0;
    u16 depth = 0;

    TexelFetchType::E fetch_type;
    TexelOrigType::E client_type;

    // If given storage_texture is valid, creates a texture view with the storage_texture as the backing
    // texture.
    RMResourceID16 storage_texture = 0;

    // Source can be `nullptr`, and in that case only storage will be allocated.
    ::variant<RMResourceID16, void *, fs::path> source;
};

struct TextureInfo {
    RMResourceID16 rmid = 0;

    GLObjectKind::E texture_kind;

    u16 width = 0;
    u16 height = 0;
    u16 depth = 0;
    u16 layers = 0; // If this is an array texture, layers >= 1.

    TexelFetchType::E fetch_type;
    TexelOrigType::E client_type;

    // If this texture is actually a 'view texture', the original texture ('storage' texture) is pointed to by
    // this id.
    RMResourceID16 storage_texture = 0;

    TextureInfo() = default;
};

static constexpr uint MAX_FRAMEBUFFER_COLOR_TEXTURES = 8;
static constexpr uint MAX_FRAGMENT_OUTPUTS = 8;

struct FramebufferInfo {
    std::array<TextureInfo, MAX_FRAMEBUFFER_COLOR_TEXTURES> color_textures;
    TextureInfo depth_texture;
    std::array<u8, MAX_FRAGMENT_OUTPUTS> current_draw_buffers;

    u32 width;
    u32 height;
};

struct CameraTransformUB {
    fo::Matrix4x4 view;             // View <- World
    fo::Matrix4x4 proj;             // Homegenous clip space <- View
    fo::Vector4 camera_position;    // Camera position wrt world space
    fo::Vector4 camera_orientation; // Camera orientation wrt world space
};

// Use fmt to place a name field
constexpr const char *camera_transform_ublock_str = R"(
uniform {} {{
    mat4 view;
    mat4 proj;
    vec4 camera_pos;
    vec4 camera_quat;
}}
)";

struct _ShaderStageKey {
    uint32_t k0, k1, k2;

    bool operator==(const _ShaderStageKey &o) const { return k0 == o.k0 && k1 == o.k1 && k2 == o.k2; }
    uint64_t hash() const noexcept { return k0 + uint64_t(~uint32_t(0)) - (k1 ^ k2); }
};

static constexpr u32 CAMERA_TRANSFORM_UBLOCK_SIZE = sizeof(CameraTransformUB);

/// Where some shader string can be stored before passing to create_shader_object`. Usually a file.
using ShaderSourceType = VariantTable<const char *, fs::path>;

// Maximum number of resource views for each type over all shader stages. Set to some non-default value if you
// want.
struct MaxCombinedShaderResourceViews {
    // The max uniform buffer bindpoints is 70 per the spec. The max uniform blocks is however very small
    // compared to that which is 12.
    u32 uniform_blocks = 70;
    u32 shader_storage_blocks = 8;
    u32 atomic_counters = 8;
    u32 texture_units = 16;
    u32 image_units = 8;

    // Set any of the above limits to one of these values so that we use glGet to retrieve the actual limits
    static constexpr u32 IGNORE_AND_GET = 9999;
};

struct MaxVsResourceAccessors {
    static constexpr u32 IGNORE_AND_GET = 9999;
    u32 uniform_blocks = 12;
    u32 shader_storage_blocks = IGNORE_AND_GET;
    u32 texture_units = 16;
    u32 vec4_attributes = 16;
};

struct MaxFsResourceAccessors {
    static constexpr u32 IGNORE_AND_GET = 9999;
    u32 uniform_blocks = 12;
    u32 shader_storage_blocks = IGNORE_AND_GET;
    u32 texture_units = 16;
};

struct MaxTcResourceAccessors {
    static constexpr u32 IGNORE_AND_GET = 9999;
    u32 shader_storage_blocks = IGNORE_AND_GET;
};

struct MaxTeResourceAccessors {
    static constexpr u32 IGNORE_AND_GET = 9999;
    u32 shader_storage_blocks = IGNORE_AND_GET;
};

struct GLObjectHandle {
    RMResourceID16 _rmid = 0;
    RMResourceID16 rmid() const { return _rmid; }
};

struct VertexBufferHandle : GLObjectHandle {
    VertexBufferHandle(RMResourceID16 rmid = 0)
        : GLObjectHandle{ rmid } {}
};

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

using VarShaderHandle = ::VariantTable<VertexShaderHandle,
                                       FragmentShaderHandle,
                                       GeometryShaderHandle,
                                       TessControlShaderHandle,
                                       TessEvalShaderHandle,
                                       ComputeShaderHandle>;

struct FBOConfig {
    struct AttachmentInfo {
        u16 width = 0;
        u16 height = 0;
        TexelFetchType::E fetch_type = TexelFetchType::INVALID;
        TexelOrigType::E client_type = TexelOrigType::INVALID;
    };

    std::array<AttachmentInfo, MAX_FBO_ATTACHMENTS> attachment_info;

    bool operator==(const FBOConfig &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

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

struct RenderManager : NonCopyable {
    fo::ArenaAllocator _allocator;

    fo::TempAllocator512 _small_allocator{ fo::memory_globals::default_allocator() };

    // Camera uniform buffer for common use.
    GETONLY(UniformBufferHandle, camera_ubo_handle) = 0;

    // Buffer hash maps

    fo::PodHash<RMResourceID16, BufferInfo> vertex_buffers =
        fo::make_pod_hash<RMResourceID16, BufferInfo>(_allocator);

    fo::PodHash<RMResourceID16, BufferInfo> element_array_buffers =
        fo::make_pod_hash<RMResourceID16, BufferInfo>(_allocator);

    fo::PodHash<RMResourceID16, BufferInfo> uniform_buffers =
        fo::make_pod_hash<RMResourceID16, BufferInfo>(_allocator);
    fo::PodHash<RMResourceID16, BufferInfo> storage_buffers =
        fo::make_pod_hash<RMResourceID16, BufferInfo>(_allocator);
    fo::PodHash<RMResourceID16, BufferInfo> pixel_pack_buffers =
        fo::make_pod_hash<RMResourceID16, BufferInfo>(_allocator);

    fo::PodHash<RMResourceID16, BufferInfo> pixel_unpack_buffers =
        fo::make_pod_hash<RMResourceID16, BufferInfo>(_allocator);

    // Array map of resource id to the above buffer tables keyed by type.
    std::array<fo::PodHash<RMResourceID16, BufferInfo> *, NUM_GL_BUFFER_KINDS> _kind_to_buffer;

    fo::PodHash<RMResourceID16, u32> _buffer_sizes = fo::make_pod_hash<RMResourceID16, u32>(_allocator);

    fo::PodHash<RMResourceID16, TextureInfo> texture_info =
        fo::make_pod_hash<RMResourceID16, TextureInfo>(_allocator);

    // Map from rmid to shader objects
    fo::PodHash<RMResourceID16, ShaderInfo> _shaders =
        fo::make_pod_hash<RMResourceID16, ShaderInfo>(_allocator);

    fo::PodHash<RMResourceID16, int> resource_to_name_map =
        fo::make_pod_hash<RMResourceID16, int>(_allocator);

    fo::OrderedMap<SamplerDesc, RMResourceID16> sampler_cache{ _allocator };

    fo::Array<RasterizerStateDesc> _cached_rasterizer_states;
    fo::Array<DepthStencilStateDesc> _cached_depth_stencil_states;
    fo::Array<BlendFunctionDesc> _cached_blendfunc_states;

    // All the currently allocated FBOs.
    fo::Vector<NewFBO> _fbos{ _allocator };
    fo::Vector<FBO> _fbos_with_glhandle{ _allocator };

    // Storing linked shaders as a GLResource64. Can get the rmid if we want. User-side doesn't need it.
    fo::PodHash<_ShaderStageKey, GLResource64, decltype(&_ShaderStageKey::hash)> _linked_shaders =
        fo::make_pod_hash<_ShaderStageKey, GLResource64>(fo::memory_globals::default_allocator(),
                                                         &_ShaderStageKey::hash);

    fo::PodHash<RMResourceID16, ShaderResourceInfo> shader_resources =
        fo::make_pod_hash<RMResourceID16, ShaderResourceInfo>(_allocator);

    // Map from rmid16 to GLuint handles
    fo::PodHash<RMResourceID16, GLResource64> _rmid16_to_res64 =
        fo::make_pod_hash<RMResourceID16, GLResource64>(_allocator);

    DEFINE_TRIVIAL_PAIR(VaoFormatAndRMID, VaoFormatDesc, format_desc, RMResourceID16, rmid);

    fo::Vector<VaoFormatAndRMID> _vaos_generated{ 0, _allocator };

    MaxCombinedShaderResourceViews max_combined_resource_views;

    // Constants that don't change after assigned. For now, these are hardcoded to
    // minimum defaults.

    RMResourceID16 _num_allocated_ids = 0;
    fo::Array<RMResourceID16> _deleted_ids{ _small_allocator }; // IDs of resources that are deleted

    i16 _num_view_ids = 0;

    fo::Array<mesh::StrippedMeshData> _stripped_mesh_datas;

    // Path to shader files are kept in a fixed string buffer. Shaders are operated on individually, and a
    // linked shader program is created only upon seeing if there aren't currently a program already linked
    // comprising of the given per-stage shaders.
    FixedStringBuffer _shader_paths;
    fo::OrderedMap<fo_ss::Buffer, RMResourceID16> _path_to_shader_rmid;

    FBO _screen_fbo;

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

void create_fbo(RenderManager &self,
                const fo::Array<RMResourceID16> &color_textures,
                RMResourceID16 depth_texture = 0);

struct RenderManagerInitConfig {
    MaxCombinedShaderResourceViews max_combined;
    MaxVsResourceAccessors max_vs;
    MaxFsResourceAccessors max_fs;
    MaxTcResourceAccessors max_tc;
    MaxTeResourceAccessors max_te;
};

void init_render_manager(RenderManager &self,
                         const RenderManagerInitConfig &conf = RenderManagerInitConfig{});

void shutdown_render_manager(RenderManager &self);

struct RasterizerStateID {
    u16 _id;
    u16 id() { return _id; }
};

struct DepthStencilStateID {
    u16 _id;
    u16 id() { return _id; }
};

struct BlendFunctionStateID {
    u16 _id;
    u16 id() { return _id; }
};

// -- Buffer creation functions

UniformBufferHandle create_uniform_buffer(RenderManager &self, const BufferCreateInfo &ci);
ShaderStorageBufferHandle create_storage_buffer(RenderManager &self, const BufferCreateInfo &ci);
VertexBufferHandle create_vertex_buffer(RenderManager &self, const BufferCreateInfo &ci);
IndexBufferHandle create_element_array_buffer(RenderManager &self, const BufferCreateInfo &ci);
PixelPackBufferHandle create_pixel_pack_buffer(RenderManager &self, const BufferCreateInfo &ci);
PixelUnpackBufferHandle create_pixel_unpack_buffer(RenderManager &self, const BufferCreateInfo &ci);

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

struct ShadersToUse {

    RMResourceID16 vs = 0;
    RMResourceID16 tc = 0;
    RMResourceID16 te = 0;
    RMResourceID16 gs = 0;
    RMResourceID16 fs = 0;
    RMResourceID16 cs = 0;

    // Call this if you want to re-use this struct for specifying another set of shaders.
    void reset() { *this = {}; }

    // Not important for user side. Converts to a key suitable for the hash-map
    _ShaderStageKey key() const {
        return { (uint32_t(vs) << 16) | uint32_t(fs),
                 (uint32_t(tc) << 16) | uint32_t(te),
                 (uint32_t(gs) << 16) | uint32_t(cs) };
    }
};

// Set the shaders to the pipeline
RMResourceID16
set_shaders(RenderManager &rm, const ShadersToUse &shaders_to_use, const char *debug_label = nullptr);

// Creates and links program if it hasn't already been linked. Returns its id.
RMResourceID16
link_shader_program(RenderManager &rm, const ShadersToUse &shaders_to_use, const char *debug_label = nullptr);

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

RasterizerStateID create_rs_state(RenderManager &self, const RasterizerStateDesc &desc);
DepthStencilStateID create_ds_state(RenderManager &self, const DepthStencilStateDesc &desc);
BlendFunctionStateID create_blendfunc_state(RenderManager &self, const BlendFunctionDesc &desc);

inline void clear_screen_fbo_color(RenderManager &self, const fo::Vector4 &color) {
    self._screen_fbo.clear_color(0, color);
}

inline void clear_screen_fbo_depth(RenderManager &self, float depth) { self._screen_fbo.clear_depth(depth); }

// Bind the resource to given bindpoint
void bind_to_bindpoint(RenderManager &self, UniformBufferHandle ubo_handle, GLuint bindpoint);

void add_backing_texture_to_view(RenderManager &self, RMResourceID16 view_fb_rmid);
void add_backing_depth_texture_to_view(RenderManager &self);

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

// This copies the level of one texture to the level of another texture. This is so named because you can't go
// directly copy texture levels or even full textures in opengl without first creating a framebuffer and
// setting the source level as the GL_READ_TARGET. Strange. I will keep a "dummy" FBO for this purpose.
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

// Set the current shader program. File names are given. Shader type is decided from the extension.
void set_shader_program(RenderManager &rm, const fo::Vector<fs::path> &shader_files);

// Similar as above, but requires specifying each shader type
void set_shader_program(RenderManager &rm,
                        const fo::Vector<fs::path> &shader_files,
                        const fo::Vector<GLObjectKind> &shader_type);

// Set depth-stencil state
void set_ds_state(RenderManager &rm, DepthStencilStateID state_id);

// Set rasterizer state
void set_rs_state(RenderManager &rm, RasterizerStateID state_id);

// Set blend-function state
// void set_blendfunc_state(RenderManager &rm, BlendFunctionStateID state_id);

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
