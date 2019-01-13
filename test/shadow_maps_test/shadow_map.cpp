#include "shadow_map.h"
#include <learnogl/gl_binding_state.h>

using namespace fo;
using namespace math;

constexpr GLenum DEPTH_TEXTURE_FORMAT = GL_DEPTH_COMPONENT32F;

static void compute_transforms(shadow_map::ShadowMap &m, const shadow_map::InitInfo &info);

namespace shadow_map {

void init(ShadowMap &m, const InitInfo &info) {
    // Generate the texture and create the fbo
    glGenTextures(1, &m.depth_map_texture);
    m.depth_texture_unit = eng::gl().bs.bind_unique(gl_desc::SampledTexture(m.depth_map_texture));

    glActiveTexture(GL_TEXTURE0 + m.depth_texture_unit);
    glBindTexture(GL_TEXTURE_2D, m.depth_map_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, DEPTH_TEXTURE_FORMAT, info.texture_size, info.texture_size);

    // Default sampling state for the depth texture is used when visualizing it.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // The actual sampling configuration used in the second pass of shadow-mapping.
    SamplerDesc depth_sampler_desc = default_sampler_desc;
    depth_sampler_desc.mag_filter = GL_NEAREST;
    depth_sampler_desc.min_filter = GL_NEAREST;
    depth_sampler_desc.addrmode_u = GL_CLAMP_TO_BORDER;
    depth_sampler_desc.addrmode_v = GL_CLAMP_TO_BORDER;
    depth_sampler_desc.compare_mode = GL_COMPARE_REF_TO_TEXTURE;
    // Comparison succeeds if argument is less or equal to sampled value.
    depth_sampler_desc.compare_func = GL_LEQUAL;

    m.comparing_sampler = eng::gl().bs.get_sampler_object(depth_sampler_desc);

    m.fbo.gen().bind().add_depth_attachment(m.depth_map_texture).set_done_creating();

    // Make the light view transform
    compute_transforms(m, info);

    m.texture_size = info.texture_size;

    m.fbo.bind_as_readable(0);
}

void set_as_draw_fbo(const ShadowMap &m, const FBO &read_fbo) { m.fbo.bind_as_writable(GLuint(read_fbo)); }

void set_as_read_fbo(const ShadowMap &m, const FBO &draw_fbo) { m.fbo.bind_as_readable(GLuint(draw_fbo)); }

void bind_comparing_sampler(ShadowMap &m) { glBindSampler(m.depth_texture_unit, m.comparing_sampler); }

void unbind_comparing_sampler(ShadowMap &m) { glBindSampler(m.depth_texture_unit, 0); }

Matrix4x4 clip_from_world_xform(ShadowMap &m) {
    return m.eye_block.clip_from_view_xform * m.eye_block.view_from_world_xform;
}

void clear(ShadowMap &m) {
    f32 max_depth = 1.0f;
    glClearBufferfv(GL_DEPTH, 0, &max_depth);
}

} // namespace shadow_map

void compute_transforms(shadow_map::ShadowMap &m, const shadow_map::InitInfo &info) {
    // Compute the world to light change of basis

    Vector3 z = negate(info.light_direction);
    Vector3 x, y;
    compute_orthogonal_complements(z, x, y);

#if 1
    // Not really needed, but doing it. Rotate the light axes by 180 deg so that y points "above" the world's
    // z = 0 plane.
    Plane3 plane(unit_y, 0.0f);
    if (dot(Vector4(plane), y) < 0.0f) {
        auto q = versor_from_axis_angle(z, pi);
        y = apply_versor(q, y);
        x = apply_versor(q, x);
    }
#endif

    Matrix4x4 light_to_world {
        Vector4(x, 0), Vector4(y, 0), Vector4(z, 0), Vector4(info.light_position, 1.0f)
    };
    m.eye_block.view_from_world_xform = inverse_rotation_translation(light_to_world);

    // Compute the light to clip space transform. Use an orthographic projection for that purpose.
    m.eye_block.clip_from_view_xform.x = Vector4 { 2.0f / info.x_extent, 0.f, 0.f, 0.f };
    m.eye_block.clip_from_view_xform.y = Vector4 { 0.f, 2.0f / info.y_extent, 0.f, 0.f };
    m.eye_block.clip_from_view_xform.z = Vector4 { 0.f, 0.f, -2.0f / info.neg_z_extent, 0.f };
    m.eye_block.clip_from_view_xform.t = Vector4 { 0.f, 0.f, -1.f, 1.f };

    m.light_direction = info.light_direction;
    m.light_position = info.light_position;
}
