#include <learnogl/gl_binding_state.h>
#include <learnogl/rng.h>
#include <scaffold/ordered_map.h>
#include <scaffold/timed_block.h>

using namespace eng::logl_internal;
using namespace fo;

static constexpr AddrUint STARTING_ALLOCATOR_SIZE = 4 * 1024;

namespace eng {

GLuint make_sampler_object(const SamplerDesc &sampler_desc);
GLuint make_vao(const VaoFormatDesc &vao_format);

void set_gl_depth_stencil_state(const DepthStencilStateDesc &ds);
void set_gl_rasterizer_state(const RasterizerStateDesc &rs);

static void create_common_vaos(BindingState &bs);

std::aligned_storage_t<sizeof(BindingState)> binding_state_storage[1];

BindingState &default_binding_state() { return *reinterpret_cast<BindingState *>(binding_state_storage); }

void init_default_binding_state(const BindingStateConfig &config) {
    string_stream::Buffer ss(memory_globals::default_allocator());
    print_callstack(ss);
    CHECK_F(false, "Called init_default_binding_state - %s", string_stream::c_str(ss));

    new (binding_state_storage) BindingState();
    default_binding_state().init(config);
}

void close_default_binding_state() { default_binding_state().~BindingState(); }

BindingState::BindingState()
    : _bs_stuff_alloc(memory_globals::default_allocator(), STARTING_ALLOCATOR_SIZE)
    , CTOR_INIT_FIELD(_sampler_cache_alloc, memory_globals::default_allocator())

    , CTOR_INIT_FIELD(_sealed, false)

    , CTOR_INIT_FIELD(_textures_bind_info,
                      _bs_stuff_alloc,
                      max_bindings_wanted::texture_image_units,
                      gl_desc::SampledTexture(0))

    , CTOR_INIT_FIELD(_uborange_bind_info,
                      _bs_stuff_alloc,
                      max_bindings_wanted::uniform_buffer,
                      gl_desc::UniformBuffer(0, 0, 0))

    , CTOR_INIT_FIELD(_ssborange_bind_info,
                      _bs_stuff_alloc,
                      max_bindings_wanted::shader_storage_buffer,
                      gl_desc::ShaderStorageBuffer(0, 0, 0))

    , CTOR_INIT_FIELD(_sampler_cache, _sampler_cache_alloc)

    , CTOR_INIT_FIELD(_rasterizer_states, memory_globals::default_allocator())

    , CTOR_INIT_FIELD(_depth_stencil_states, memory_globals::default_allocator())

    , CTOR_INIT_FIELD(_vaos_generated, 0, _bs_stuff_alloc)

    , CTOR_INIT_FIELD(_fbo_storage, 0, _bs_stuff_alloc)

    , CTOR_INIT_FIELD(_current_rasterizer_state, ~u32(0))

    , CTOR_INIT_FIELD(_current_depth_stencil_state, ~u32(0))

    , CTOR_INIT_FIELD(_current_draw_fbo, nullptr)
    , CTOR_INIT_FIELD(_current_read_fbo, nullptr)

{}

void BindingState::init(const BindingStateConfig &config) {
    CHECK_EQ_F(_per_camera_ubo.handle(), 0u, "BindingState already initialized?");

    LOG_SCOPE_F(INFO, "Initializing BindingState");

    // Set up texture image units info
    {
        GLint max_texture_image_units;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_image_units);
        LOG_F(INFO, "GL_MAX_TEXTURE_IMAGE_UNITS = %i", max_texture_image_units);

        CHECK_F(max_bindings_wanted::texture_image_units <= max_texture_image_units);

        max_texture_image_units = std::min(max_bindings_wanted::texture_image_units, max_texture_image_units);
        LOG_F(INFO, "-- But max chosen = %i", max_texture_image_units);

        _textures_bind_info.reserve_space((u32)max_texture_image_units);
    }

    // Set up uniform binding points info
    {
        GLint max_uniform_buffer_bindings;
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &max_uniform_buffer_bindings);
        LOG_F(INFO, "GL_MAX_UNIFORM_BUFFER_BINDINGS = %i", max_uniform_buffer_bindings);

        CHECK_LE_F(max_bindings_wanted::uniform_buffer, max_uniform_buffer_bindings);

        max_uniform_buffer_bindings =
            std::min(max_bindings_wanted::uniform_buffer, max_uniform_buffer_bindings);
        LOG_F(INFO, "-- But max chosen = %i", max_uniform_buffer_bindings);

        _uborange_bind_info.reserve_space((u32)max_uniform_buffer_bindings);
    }

    {
        // Set up shader storage binding points info
        GLint max_shader_storage_buffer_bindings = 0;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &max_shader_storage_buffer_bindings);
        LOG_F(INFO, "GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS = %i", max_shader_storage_buffer_bindings);

        CHECK_LE_F(max_bindings_wanted::shader_storage_buffer, max_shader_storage_buffer_bindings);

        max_shader_storage_buffer_bindings =
            std::min(max_bindings_wanted::shader_storage_buffer, max_shader_storage_buffer_bindings);
        LOG_F(INFO, "-- But max chosen = %i", max_shader_storage_buffer_bindings);

        _ssborange_bind_info.reserve_space((u32)max_shader_storage_buffer_bindings);
    }

    // xyspoon: Add more here as required

    // Reserve some space for vao formats
    reserve(_vaos_generated, config.expected_unique_vaos);

    // Initialize the per_camera ubo
    if (true) {
        GLuint per_camera_ubo_handle;
        glGenBuffers(1, &per_camera_ubo_handle);
        glBindBuffer(GL_UNIFORM_BUFFER, per_camera_ubo_handle);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(PerCameraUBlockFormat), nullptr, GL_DYNAMIC_DRAW);
        _per_camera_ubo = gl_desc::UniformBuffer(per_camera_ubo_handle, 0, sizeof(PerCameraUBlockFormat));
        _per_camera_ubo_binding = this->bind_unique(_per_camera_ubo);
    }

    // Create the no-attribute vao, and bind it just coz.
    {
        CHECK_EQ_F(size(_vaos_generated), 0u);

        auto vao_format_desc = VaoFormatDesc::from_attribute_formats({});
        _no_attrib_vao = get_vao(vao_format_desc);

        LOG_F(INFO, "No attrib VAO = %u", _no_attrib_vao);
    }

    // Create some commonly-used VAO formats
    { create_common_vaos(*this); }

    // Set both draw and read FBOs to screen fbo
    {
        _screen_fbo.init_from_default_framebuffer();
        // _screen_fbo._name = "@Default_FBO";
        _current_draw_fbo = &_screen_fbo;
        _current_read_fbo = &_screen_fbo;
        fo::reserve(_fbo_storage, config.expected_fbo_count);
    }
}

BindingState::~BindingState() {}

GLuint BindingState::bind_unique(const gl_desc::SampledTexture &texture) {
    CHECK_F(!_sealed, "Cannot bind new resource. BindingState already sealed");

    auto &binds = _textures_bind_info;

    auto already_bound = binds.get_bound(texture);

    if (already_bound) {
        GLuint binding_point = already_bound.value();
        return binding_point;
    }

    if (!binds.space()) {
        string_stream::Buffer ss(memory_globals::default_allocator());
        print_callstack(ss);
        LOG_F(ERROR, "\n%s", string_stream::c_str(ss));
    }

    CHECK_F((bool)binds.space(),
            "%s - Exhausted all binding points. [_max_bindpoints = %u]",
            __PRETTY_FUNCTION__,
            binds.max_bindpoints());

    GLuint binding_point = binds.add_new(texture).value();
    glActiveTexture(binding_point + GL_TEXTURE0);

    return binding_point;
}

GLuint BindingState::bind_unique(const gl_desc::UniformBuffer &ubo) {
    CHECK_F(!_sealed, "Cannot bind new resource. BindingState already sealed");

    assert(ubo.handle() != 0 && "");

    auto &binds = _uborange_bind_info;
    auto already_bound = binds.get_bound(ubo);

    if (already_bound) {
        GLuint binding_point = already_bound.value();
        glBindBuffer(GL_UNIFORM_BUFFER, binding_point);
        return binding_point;
    }

    CHECK_F((bool)binds.space(),
            "%s - Exhausted all binding points. [_max_bindpoints = %u]",
            __PRETTY_FUNCTION__,
            binds.max_bindpoints());

    GLuint binding_point = binds.add_new(ubo).value();
    glBindBuffer(GL_UNIFORM_BUFFER, ubo._handle);
    glBindBufferRange(GL_UNIFORM_BUFFER, binding_point, ubo._handle, ubo._offset, ubo._size);

    return binding_point;
}

GLuint BindingState::bind_unique(const gl_desc::ShaderStorageBuffer &ssbo) {
    CHECK_F(!_sealed, "Cannot bind new resource. BindingState already sealed");

    assert(ssbo.handle() != 0 && "");

    auto &binds = _ssborange_bind_info;
    auto already_bound = binds.get_bound(ssbo);

    if (already_bound) {
        GLuint binding_point = already_bound.value();
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, binding_point);
        return binding_point;
    }

    CHECK_F((bool)binds.space(),
            "%s - Exhausted all binding points. [_max_bindpoints = %u]",
            __PRETTY_FUNCTION__,
            binds.max_bindpoints());

    GLuint binding_point = binds.add_new(ssbo).value();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo._handle);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, binding_point, ssbo._handle, ssbo._offset, ssbo._size);

    return binding_point;
}

GLuint BindingState::reserve_sampler_bindpoint() {
    ::optional<GLuint> bindpoint = _textures_bind_info.reserve_new();
    CHECK_F(bool(bindpoint), "Failed to reserve bindpoint");
    return bindpoint.value();
}

GLuint BindingState::reserve_uniform_bindpoint() {
    ::optional<GLuint> bindpoint = _uborange_bind_info.reserve_new();
    CHECK_F(bool(bindpoint), "Failed to reserve bindpoint");
    return bindpoint.value();
}

GLuint BindingState::reserve_ssbo_bindpoint() {
    ::optional<GLuint> bindpoint = _ssborange_bind_info.reserve_new();
    CHECK_F(bool(bindpoint), "Failed to reserve bindpoint");
    return bindpoint.value();
}

void BindingState::release_bind(const gl_desc::SampledTexture &texture) {
    CHECK_F(!_sealed, "Must not release after being sealed");
    _textures_bind_info.release(texture);
}

void BindingState::release_bind(const gl_desc::UniformBuffer &ubo) {
    CHECK_F(!_sealed, "Must not release after being sealed");
    _uborange_bind_info.release(ubo);
}

GLuint BindingState::get_sampler_object(const SamplerDesc &sampler_desc) {
    auto it = get(_sampler_cache, sampler_desc);
    if (it != end(_sampler_cache)) {
        return it->v;
    }
    return set(_sampler_cache, sampler_desc, make_sampler_object(sampler_desc))->v;
}

u32 BindingState::add_rasterizer_state(const RasterizerStateDesc &rs) {
    for (u32 i = 0; i < size(_rasterizer_states); ++i) {
        if (_rasterizer_states[i] == rs) {
            return i;
        }
    }
    push_back(_rasterizer_states, rs);
    return size(_rasterizer_states) - 1;
}

void BindingState::set_rasterizer_state(u32 rs_number) {
    DCHECK_LT_F(rs_number, size(_rasterizer_states), "Invalid rasterizer state number");
    const RasterizerStateDesc &rs = _rasterizer_states[rs_number];

    static_assert(GL_NONE != GL_FRONT_AND_BACK && GL_NONE != GL_BACK && GL_NONE != GL_BACK,
                  "Just to be sure");
    set_gl_rasterizer_state(rs);
}

u32 BindingState::add_depth_stencil_state(const DepthStencilStateDesc &ds) {
    for (u32 i = 0; i < size(_depth_stencil_states); ++i) {
        if (memcmp(&ds, &_depth_stencil_states[i], sizeof(DepthStencilStateDesc)) == 0) {
            return i;
        }
    }
    fo::push_back(_depth_stencil_states, ds);
    return fo::size(_depth_stencil_states) - 1;
}

void BindingState::set_depth_stencil_state(u32 ds_number) {
    set_gl_depth_stencil_state(_depth_stencil_states[ds_number]);
}

GLuint BindingState::get_vao(const VaoFormatDesc &vao_format_desc) {
    for (u32 i = 0; i < size(_vaos_generated); ++i) {
        if (_vaos_generated[i].format_desc == vao_format_desc) {
            return _vaos_generated[i].vao_deleter.handle();
        }
    }
    // Need to add new format
    push_back(_vaos_generated,
              VaoFormatAndObject{ vao_format_desc, VertexArrayDeleter(make_vao(vao_format_desc)) });

    // LOG_F(INFO, "Vaos generated by gl_binding_state = %u", size(_vaos_generated));
    return back(_vaos_generated).vao_deleter.handle();
}

void BindingState::seal() { _sealed = true; }

GLuint make_sampler_object(const SamplerDesc &sampler_desc) {
    GLuint sampler_object;
    glGenSamplers(1, &sampler_object);

    LOG_F(INFO, "Creating a new sampler");

    if (sampler_desc.mag_filter != default_sampler_desc.mag_filter) {
        glSamplerParameteri(sampler_object, GL_TEXTURE_MAG_FILTER, sampler_desc.mag_filter);
    }

    if (sampler_desc.min_filter != default_sampler_desc.min_filter) {
        glSamplerParameteri(sampler_object, GL_TEXTURE_MIN_FILTER, sampler_desc.min_filter);
    }

    if (sampler_desc.addrmode_u != default_sampler_desc.addrmode_u) {
        glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_S, sampler_desc.addrmode_u);
    }

    if (sampler_desc.addrmode_v != default_sampler_desc.addrmode_v) {
        glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_T, sampler_desc.addrmode_v);
    }

    if (sampler_desc.addrmode_w != default_sampler_desc.addrmode_w) {
        glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_R, sampler_desc.addrmode_w);
    }

    // Set the border color unconditionally
    glSamplerParameterfv(sampler_object, GL_TEXTURE_BORDER_COLOR, sampler_desc.border_color);

    if (sampler_desc.min_lod != default_sampler_desc.min_lod) {
        glSamplerParameterf(sampler_object, GL_TEXTURE_MIN_LOD, sampler_desc.min_lod);
    }

    if (sampler_desc.max_lod != default_sampler_desc.max_lod) {
        glSamplerParameterf(sampler_object, GL_TEXTURE_MAX_LOD, sampler_desc.max_lod);
    }

    if (sampler_desc.compare_mode != GL_NONE) {
        // Set the compare func and mode together
        glSamplerParameteri(sampler_object, GL_TEXTURE_COMPARE_MODE, sampler_desc.compare_mode);
        glSamplerParameteri(sampler_object, GL_TEXTURE_COMPARE_FUNC, sampler_desc.compare_func);
    }

    if (sampler_desc.mip_lod_bias != default_sampler_desc.mip_lod_bias) {
        glSamplerParameterf(sampler_object, GL_TEXTURE_LOD_BIAS, sampler_desc.mip_lod_bias);
    }

    if (sampler_desc.max_anisotropy != 0.0f) {
        glSamplerParameterf(sampler_object, GL_TEXTURE_MAX_ANISOTROPY, sampler_desc.max_anisotropy);
    }

    return sampler_object;
}

GLuint make_vao(const VaoFormatDesc &vao_format) {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    for (u32 i = 0; i < ARRAY_SIZE(vao_format.compressed_attrib_formats); ++i) {
        u64 compressed = vao_format.compressed_attrib_formats[i];
        if (compressed == 0u) {
            continue;
        }
        VaoAttributeFormat attrib_format = VaoAttributeFormat::uncompress(compressed);
        attrib_format.set_format_for_gl(i);
        glEnableVertexAttribArray(i);

        if (attrib_format.vbo_binding_point == 0u) {
            glVertexAttribBinding(i, 0u);
        } else {
            CHECK_NE_F(attrib_format.instances_before_advancing,
                       0u,
                       "Only using non-0 binding point for instanced attributes, but this attribute is not "
                       "instanced");

            glVertexAttribBinding(i, attrib_format.vbo_binding_point);
            glVertexBindingDivisor(attrib_format.vbo_binding_point, attrib_format.instances_before_advancing);
        }
    }

    // LOG_F(INFO, "Generated VAO %u of description - %p", vao, &vao_format);

    return vao;
}

void set_gl_rasterizer_state(const RasterizerStateDesc &rs) {
    glFrontFace(rs.front_face);

    if (rs.cull_side == GL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(rs.cull_side);
    }

    glPolygonMode(GL_FRONT_AND_BACK, rs.fill_mode);

    if (rs.enable_depth_clamp) {
        glEnable(GL_DEPTH_CLAMP);
    } else {
        glDisable(GL_DEPTH_CLAMP);
    }

    if (rs.enable_line_smoothing) {
        glEnable(GL_LINE_SMOOTH);
    } else {
        glDisable(GL_LINE_SMOOTH);
    }

    // Depth bias specification. The amount to add is given by constant_depth_bias * L +
    // slope_scaled_depth_bias * max_depth_slope. L is either the smallest represented value in the unorm
    // format of the depth buffer, or, if it's a floating point depth buffer, the smallest resolvable depth
    // over the primitive.
    if (rs.slope_scaled_depth_bias != 0.0f && rs.constant_depth_bias != 0) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(rs.slope_scaled_depth_bias, (f32)rs.constant_depth_bias);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void set_gl_depth_stencil_state(const DepthStencilStateDesc &ds) {
    if (ds.enable_depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    if (ds.enable_depth_write) {
        glDepthMask(GL_TRUE);
    } else {
        glDepthMask(GL_FALSE);
    }

    if (ds.enable_stencil_test) {
        glEnable(GL_STENCIL_TEST);
    }

    if (memcmp(&ds.front_face_ops, &ds.back_face_ops, sizeof(ds.front_face_ops)) == 0) {
        glStencilOp(ds.front_face_ops.fail, ds.front_face_ops.pass, ds.front_face_ops.pass_but_dfail);
        glStencilFunc(ds.front_face_ops.compare_func,
                      ds.front_face_ops.stencil_ref,
                      ds.front_face_ops.stencil_read_mask);
    } else {
        glStencilOpSeparate(
            GL_FRONT, ds.front_face_ops.fail, ds.front_face_ops.pass, ds.front_face_ops.pass_but_dfail);
        glStencilOpSeparate(
            GL_BACK, ds.back_face_ops.fail, ds.back_face_ops.pass, ds.back_face_ops.pass_but_dfail);

        glStencilFuncSeparate(GL_FRONT,
                              ds.front_face_ops.compare_func,
                              ds.front_face_ops.stencil_ref,
                              ds.front_face_ops.stencil_read_mask);
        glStencilFuncSeparate(GL_FRONT,
                              ds.back_face_ops.compare_func,
                              ds.back_face_ops.stencil_ref,
                              ds.back_face_ops.stencil_read_mask);
    }
}

bool framebuffer_complete(GLuint fbo) {
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_F(ERROR, "Incomplete framebuffer: %u", fbo);
        switch (status) {
        case GL_FRAMEBUFFER_UNDEFINED:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_UNDEFINED");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER");
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_UNSUPPORTED");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE");
        case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
            LOG_F(ERROR, "-- GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS");
            break;
        default:
            LOG_F(ERROR, "-- unspecified error");
        }
        return false;
    }
    return true;
}

void create_common_vaos(BindingState &bs) {
    // [position: vec3]
    auto vao_format = VaoFormatDesc::from_attribute_formats({ VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, 0) });
    bs.pos_vao = bs.get_vao(vao_format);

    // [position: vec2]
    vao_format = VaoFormatDesc::from_attribute_formats({ VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, 0) });
    bs.pos2d_vao = bs.get_vao(vao_format);

    // [position: vec3, normal: vec3, texcoord_2d: vec3]

    vao_format = VaoFormatDesc::from_attribute_formats(
        { VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, 0),
          VaoAttributeFormat(3, GL_FLOAT, GL_FALSE, sizeof(Vector3)),
          VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, sizeof(Vector3) + sizeof(Vector3)) });

    bs.pos_normal_uv_vao = bs.get_vao(vao_format);
}

DEFINE_TRIVIAL_PAIR(BoundUBO, GLuint, bindpoint, gl_desc::UniformBuffer, ubo_desc);
DEFINE_TRIVIAL_PAIR(BoundSSBO, GLuint, bindpoint, gl_desc::ShaderStorageBuffer, ssbo_desc);
DEFINE_TRIVIAL_PAIR(BoundTexture, GLuint, bindpoint, gl_desc::SampledTexture, texture_desc);

void BindingState::save_bindings(SavedBindingsHeader *saved, u32 allocated_bytes) {
    TIMED_BLOCK;

    i32 data_block_size = (i32)allocated_bytes - (i32)sizeof(SavedBindingsHeader);
    CHECK_GE_F(data_block_size, 0, "No extra space allocated to hold saved bindings");

    saved->data_block_size = (u32)data_block_size;
    saved->remaining_size = saved->data_block_size;

    saved->rasterizer_state_number = _current_rasterizer_state;
    saved->ds_state_number = _current_depth_stencil_state;

    u8 *cursor = (u8 *)saved->data;

    const u8 *end = cursor + saved->data_block_size;

    fn_ have_space = [&](u32 allocated_bytes) { return end - cursor >= allocated_bytes; };

    u16 count = 0;

    fn_ store_ubo_bindpoints = [&](GLuint bindpoint, const gl_desc::UniformBuffer &ubo_range) {
        if (have_space(sizeof(BoundUBO))) {
            BoundUBO binding(bindpoint, ubo_range);
            memcpy(cursor, &binding, sizeof(binding));
            cursor += sizeof(binding);
            count++;
            return true;
        }
        return false;
    };

    // @rksht : SSBO and UBO are handled exactly the same way. Just have a single "ShaderAccessBuffer"
    // description for both instead of this redundancy.
    fn_ store_ssbo_bindpoints = [&](GLuint bindpoint, const gl_desc::ShaderStorageBuffer &ssbo_range) {
        if (have_space(sizeof(BoundSSBO))) {
            BoundSSBO binding(bindpoint, ssbo_range);
            memcpy(cursor, &binding, sizeof(binding));
            cursor += sizeof(ssbo_range);
            count++;
            return true;
        }
        return false;
    };

    fn_ store_texture_bindpoints = [&](GLuint bindpoint, const gl_desc::SampledTexture &texture) {
        if (have_space(sizeof(BoundTexture))) {
            BoundTexture binding(bindpoint, texture);
            memcpy(cursor, &binding, sizeof(binding));
            cursor += sizeof(binding);
            count++;
            return true;
        }
        return false;
    };

    bool stored_all = true;

    saved->offset_ubo_ranges = 0;
    saved->offset_ssbo_ranges = 0;
    saved->offset_sampled_textures = 0;

    count = 0;
    saved->offset_ubo_ranges = u16(cursor - (u8 *)saved); // == 0
    stored_all = _uborange_bind_info.iterate_by_function(store_ubo_bindpoints);
    saved->num_ubo_ranges = count;
    CHECK_F(stored_all, "Need more space to store currently bound UBOs");

    count = 0;
    saved->offset_ssbo_ranges = u16(cursor - (u8 *)saved);
    stored_all = _ssborange_bind_info.iterate_by_function(store_ssbo_bindpoints);
    saved->num_ssbo_ranges = count;
    CHECK_F(stored_all, "Need more space to store currently bound SSBOs");

    count = 0;
    saved->offset_sampled_textures = u16(cursor - (u8 *)saved);
    stored_all = _textures_bind_info.iterate_by_function(store_texture_bindpoints);
    saved->num_sampled_textures = count;
    CHECK_F(stored_all, "Need more space to store currently bound textures");

    saved->remaining_size = (u32)(end - cursor);
}

void BindingState::restore_bindings(const SavedBindingsHeader *saved) {
    {
        BoundUBO *pair = (BoundUBO *)(saved->data + saved->offset_ubo_ranges);
        for (u32 i = 0; i < saved->num_ubo_ranges; ++i) {
            _uborange_bind_info.set(pair[i].bindpoint, pair[i].ubo_desc);
        }
    }

    {
        BoundSSBO *pair = (BoundSSBO *)(saved->data + saved->offset_ssbo_ranges);
        for (u32 i = 0; i < saved->num_sampled_textures; ++i) {
            _ssborange_bind_info.set(pair[i].bindpoint, pair[i].ssbo_desc);
        }
    }

    {
        BoundTexture *pair = (BoundTexture *)(saved->data + saved->offset_sampled_textures);
        for (u32 i = 0; i < saved->num_ssbo_ranges; ++i) {
            _textures_bind_info.set(pair[i].bindpoint, pair[i].texture_desc);
        }
    }

    set_rasterizer_state(saved->rasterizer_state_number);
    set_depth_stencil_state(saved->ds_state_number);
}

void BindingState::set_blend_function(i32 output_number, const BlendFunctionDesc &func) {
    auto &fbo = *_current_draw_fbo;

    if (fbo._fbo_handle != 0) {
        DCHECK_LT_F(output_number,
                    (i32)fbo.num_attachments(),
                    "Invalid attachment number for currently bound framebuffer ");
    } else {
        DCHECK_EQ_F(output_number, 0, "Default framebuffer only has 1 attachment (attachment 0)");
        DCHECK_EQ_F(_current_draw_fbo, &_screen_fbo);

        // For default framebuffer, the "0" draw buffer is a denoted by a this symbol.
        output_number = 0;
    }

    if (func.blend_op == BlendOp::BLEND_DISABLED) {
        glDisablei(GL_BLEND, output_number);
        return;
    }

    glEnablei(GL_BLEND, output_number);
    glBlendFuncSeparatei(output_number,
                         func.src_rgb_factor,
                         func.dst_rgb_factor,
                         func.src_alpha_factor,
                         func.dst_alpha_factor);
    glBlendEquationi(output_number, func.blend_op);
}

void BindingState::print_log() const {
    LOG_SCOPE_F(INFO, "BindingState log --");
    TempAllocator1024 ta(fo::memory_globals::default_allocator());
    fo::Array<fo::ArenaInfo> arenas_allocated(ta);

    fo::reserve(arenas_allocated, 10);

    _bs_stuff_alloc.get_chain_info(arenas_allocated);

    LOG_F(INFO, "Arena info - ");

    for (u32 i = 0; i < size(arenas_allocated); ++i) {
        auto &arena_info = arenas_allocated[i];

        printf("%*c Arena %u, BufferSize = %lu, TotalAlloc = %lu\n",
               i * 2,
               ' ',
               i,
               (u32)arena_info.buffer_size,
               (u32)arena_info.total_allocated);
    }
}

void set_texture_parameters(GLuint texture, const SamplerDesc &sampler_desc) {
    DCHECK_NE_F(texture, 0u);

    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, sampler_desc.mag_filter);
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, sampler_desc.min_filter);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, sampler_desc.addrmode_u);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, sampler_desc.addrmode_v);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_R, sampler_desc.addrmode_w);
    glTextureParameterfv(texture, GL_TEXTURE_BORDER_COLOR, sampler_desc.border_color);
    glTextureParameterf(texture, GL_TEXTURE_MIN_LOD, sampler_desc.min_lod);
    glTextureParameterf(texture, GL_TEXTURE_MAX_LOD, sampler_desc.max_lod);
    glTextureParameteri(texture, GL_TEXTURE_COMPARE_MODE, sampler_desc.compare_mode);
    glTextureParameteri(texture, GL_TEXTURE_COMPARE_FUNC, sampler_desc.compare_func);
    glTextureParameterf(texture, GL_TEXTURE_LOD_BIAS, sampler_desc.mip_lod_bias);
    // glTextureParameterf(texture, GL_TEXTURE_MAX_ANISOTROPY, sampler_desc.max_anisotropy);
}

// FBO class impl

bool FBO::has_depth_attachment() const { return _depth_texture != 0; }

// Initialize an FBO object from a GLFW surface. This is a no-op and for readability only. You cannot
// "read" from the textures (color/depth) attached to the on-screen framebuffer.
FBO &FBO::init_from_default_framebuffer() { return *this; }

FBO &FBO::gen(const char *name) {
    glGenFramebuffers(1, &_fbo_handle);
    _num_attachments = 0;
    memset(_color_buffer_textures, 0, MAX_FBO_ATTACHMENTS * sizeof(i32));
    memset(_color_buffer_textures, 0, MAX_FBO_ATTACHMENTS * sizeof(i32));

    _num_attachments = 0;
    _depth_texture = 0;
    _name = name;

    return *this;
}

FBO &FBO::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo_handle);
    return *this;
}
const FBO &FBO::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo_handle);
    return *this;
}

const FBO &FBO::bind_as_writable(GLuint readable_fbo) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readable_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_handle);
    return *this;
}

const FBO &FBO::bind_as_readable(GLuint writable_fbo) const {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, writable_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _fbo_handle);
    return *this;
}

FBO &FBO::bind_as_writable(GLuint readable_fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readable_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_handle);
    return *this;
}

FBO &FBO::bind_as_readable(GLuint writable_fbo) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, writable_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _fbo_handle);
    return *this;
}

void FBO::_validate_attachment(i32 attachment_number) {
    assert(!_is_done_creating());
    DCHECK_EQ_F(
        _color_buffer_textures[attachment_number], 0u, "Texture already attached to this color buffer");
}

FBO &FBO::add_attachment(i32 attachment_number, GLuint texture, i32 level, GLenum texture_type) {
    _validate_attachment(attachment_number);
    glNamedFramebufferTexture(_fbo_handle, GL_COLOR_ATTACHMENT0 + attachment_number, texture, level);
    _color_buffer_textures[_num_attachments++] = texture;
    return *this;
}

FBO &FBO::add_attachment_rbo(i32 attachment_number, GLuint rbo) {
    _validate_attachment(attachment_number);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glFramebufferRenderbuffer(
        GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + attachment_number, GL_RENDERBUFFER, rbo);
    ++_num_attachments;
    return *this;
}

// Add a texture as depth attachment
FBO &FBO::add_depth_attachment(GLuint texture, GLenum depth_attachment, i32 level, GLenum texture_type) {
    CHECK_EQ_F(_depth_texture, 0u, "A depth attachment has already been added");
    glNamedFramebufferTexture(_fbo_handle, depth_attachment, texture, level);
    _depth_texture = texture;
    return *this;
}

// Add an rbo as depth attachment
FBO &FBO::add_depth_attachment_rbo(GLuint rbo, GLenum depth_attachment) {
    CHECK_EQ_F(_depth_texture, 0u, "A depth attachment has already been added");
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, depth_attachment, GL_RENDERBUFFER, rbo);
    _depth_texture = rbo;
    return *this;
}

bool FBO::_is_done_creating() const { return _num_attachments < 0; }

// Valid after creation complete only
u32 FBO::num_attachments() const { return std::abs(_num_attachments); }

FBO &FBO::set_draw_buffers(i32 *attachment_of_output, size_t count) {
    if (_fbo_handle == 0) {
        // Default framebuffer. Do nothing.
        return *this;
    }

    bool ok = true;

    for (size_t i = 0; i < count; ++i) {
        i32 a = attachment_of_output[i];

        if (a == NO_ATTACHMENT) {
            continue;
        }

        if (a >= MAX_FBO_ATTACHMENTS || _color_buffer_textures[a] == 0) {
            LOG_F(ERROR, "FBO: %s, No texture or rbo bound at attachment number: %i", _name, a);
            ok = false;
        }
    }

    DCHECK_F(ok, "See errors");

    GLenum gl_attachments[MAX_FBO_ATTACHMENTS] = {};

    for (size_t i = 0; i < count; ++i) {
        i32 a = attachment_of_output[i];
        if (a == NO_ATTACHMENT) {
            gl_attachments[i] = GL_NONE;
        } else {
            gl_attachments[i] = GL_COLOR_ATTACHMENT0 + a;
        }
    }

    glDrawBuffers((GLsizei)count, gl_attachments);
    return *this;
}

FBO &FBO::set_read_buffer(i32 attachment_number) {
    if (_fbo_handle != 0) {
        // Check that attachment has a texture or rbo
        CHECK_F(_color_buffer_textures[attachment_number] != 0,
                "No texture attached to attachment %i",
                attachment_number);

        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment_number);
    } else {
        glReadBuffer(GL_BACK_LEFT);
    }
    return *this;
}

// Clear attached depth buffer (if any) with given value. Framebuffer should be currently bound as
// writable.
FBO &FBO::clear_depth(f32 d) {
    if (_fbo_handle != 0) {
        DCHECK_F(_depth_texture != 0, "No depth attachment");
    }
    glClearBufferfv(GL_DEPTH, 0, &d);
    return *this;
}

FBO &FBO::clear_color(i32 attachment_number, fo::Vector4 color) {
    if (_fbo_handle == 0) {
        DCHECK_F(attachment_number == 0, "Default FBO has only one color attachment");
        glClearBufferfv(GL_COLOR, 0, (const float *)&color);
        return *this;
    }

    DCHECK_F(attachment_number < MAX_FBO_ATTACHMENTS);
    DCHECK_F(_color_buffer_textures[attachment_number] != 0,
             "No texture bound to attachment %i",
             attachment_number);
    glClearBufferfv(GL_COLOR, attachment_number, (const GLfloat *)&color);
    return *this;
}

// Call after you're done adding attachments
FBO &FBO::set_done_creating() {
    _num_attachments = -_num_attachments;
    CHECK_F(framebuffer_complete(_fbo_handle), "Incomplete FBO - %s", _name);
    set_fbo_label(_fbo_handle, _name);
    return *this;
}

GLuint FBO::color_attachment_texture(int i) const { return _color_buffer_textures[i]; }
GLuint FBO::depth_attachment_texture() const { return _depth_texture; }

SetInputOutputFBO::SetInputOutputFBO() {
    std::fill(attachment_of_output.begin(), attachment_of_output.end(), FBO::NO_ATTACHMENT);
}

void SetInputOutputFBO::reset_struct() {
    input_fbo = output_fbo = nullptr;
    std::fill(attachment_of_output.begin(), attachment_of_output.end(), FBO::NO_ATTACHMENT);
    read_attachment_number = FBO::NO_ATTACHMENT;
}

void set_input_output_fbos(SetInputOutputFBO &config) {
    config.input_fbo->bind_as_readable(GLuint(*config.output_fbo));
    config.input_fbo->set_read_buffer(config.read_attachment_number);
    config.output_fbo->set_draw_buffers(config.attachment_of_output.data(),
                                        config.attachment_of_output.size());
}

} // namespace eng
