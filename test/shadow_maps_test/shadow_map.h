#pragma once

#include "demo_types.h"

namespace shadow_map {

struct ShadowMap {
    FBO fbo;
    GLuint depth_map_texture;
    GLuint comparing_sampler;
    GLuint depth_texture_unit;

    fo::Vector3 light_position;
    fo::Vector3 light_direction;

    u32 texture_size; // Texture is a square

    // The world, view, projection matrices used during the build-shadow-map pass
    uniform_formats::EyeBlock eye_block;

    // This transform is used in the lighting pass to get the corresponding texture coordinate of the fragment
    // into the shadow map.
    fo::Matrix4x4 clip_from_world;
};

// The info required to set up the shadow map transforms
struct InitInfo {
    fo::Vector3 light_direction;
    fo::Vector3 light_position;
    u32 texture_size;
    f32 x_extent;
    f32 y_extent;
    f32 neg_z_extent;
};

void init(ShadowMap &m, const InitInfo &init_info);
void set_as_draw_fbo(const ShadowMap &m, const FBO &read_fbo);
void set_as_read_fbo(const ShadowMap &m, const FBO &draw_fbo);

void bind_comparing_sampler(ShadowMap &m);
void unbind_comparing_sampler(ShadowMap &m);

// Clears the depth texture (fills with 1.0f)
void clear(ShadowMap &m);

inline const fo::Matrix4x4 &light_from_world_xform(const ShadowMap &m) {
    return m.eye_block.view_from_world_xform;
}

inline const fo::Matrix4x4 &clip_from_light_xform(const ShadowMap &m) {
    return m.eye_block.clip_from_view_xform;
}

// Returns a transform that takes a point in world space to clip space from light's point of view.
Matrix4x4 clip_from_world_xform(ShadowMap &m);

} // namespace shadow_map
