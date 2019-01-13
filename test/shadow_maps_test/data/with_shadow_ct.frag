#version 430 core

// Shader takes the albedo from a uniform block, not a texture.

/* __macro__

DEPTH_TEXTURE_UNIT = int
DIR_LIGHTS_LIST_UBLOCK_BINDING = int
NUM_DIR_LIGHTS = int
PER_OBJECT_UBLOCK_BINDING = int
CAMERA_ETC_UBLOCK_BINDING = int
SHADOW_RELATED_PARAMS_BINDING = int

*/

#include "definitions.inc.glsl"

in VsOut {
    vec3 pos_w;
    vec3 normal_w;
    vec3 tangent_w;
    vec2 st;
} fs_in;

struct BoundingSphere {
    vec3 position;
    float radius;
};

// The shadow transform takes a position in world space to clip space from the point of view of casting light.
layout(binding = SHADOW_RELATED_PARAMS_BINDING, std140) uniform ShadowRelated {
    mat4 shadow_xform;
    BoundingSphere scene_bs;
};

layout(binding = DEPTH_TEXTURE_UNIT) uniform sampler2DShadow comparing_sampler;


layout(binding = DIR_LIGHTS_LIST_UBLOCK_BINDING, std140) uniform DirLightsList {
    DirLight dir_lights[NUM_DIR_LIGHTS];
};

layout(binding = PER_OBJECT_UBLOCK_BINDING, std140) uniform ublock_PerObject {
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material;
};

layout(binding = CAMERA_ETC_UBLOCK_BINDING, std140) uniform ublock_EyeBlock {
    mat4 view_from_world_xform;
    mat4 clip_from_view_xform;
    vec3 eye_pos;
    float frame_interval;
};

out vec4 fc;

float calc_attenuation(float d, float falloff_start, float falloff_end) {
    float ratio = (falloff_end - d) / (falloff_end - falloff_start);
    return clamp(ratio, 0.0, 1.0);
}

vec3 schlick_fresnel(vec3 R0, vec3 normal, vec3 light_vec) {
    float cos_incident_angle = clamp(dot(normal, light_vec), 0.0, 1.0);
    float f0 = 1.0 - cos_incident_angle;
    vec3 reflection_fraction = R0 + (1.0 - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflection_fraction;
}

vec3 filmic_tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 blinn_phong_shade(vec3 light_strength, vec3 light_vec, vec3 normal, vec3 to_eye, Material mat) {
    const float m = mat.shininess * 256.0;
    vec3 half_vec = normalize(to_eye + light_vec);

    const float ndoth = max(0.0, dot(half_vec, normal));

    float roughness_factor = (m + 8.0) * pow(ndoth, m) / 8.0;
    vec3 fresnel_factor = schlick_fresnel(mat.fresnel_R0, half_vec, light_vec);
    vec3 spec_albedo = roughness_factor * fresnel_factor;

    // Our spec formula result goes outside [0.0 .. 1.0] range, but we are doing LDR rendering, so forcefully
    // scale it down a little. A primitive tone-mapping function, one could say.
    // spec_albedo = spec_albedo / (spec_albedo + 1.0);
    vec3 final_color = (mat.diffuse_albedo.rgb + spec_albedo) * light_strength;
    return final_color;
}

vec3 calc_dir_light_contrib(DirLight L, Material mat, vec3 normal, vec3 to_eye) {
    // The light vector aims opposite the dir the light rays travel.
    vec3 lightVec = -L.direction;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    vec3 lightStrength = L.diffuse * ndotl;

    return blinn_phong_shade(lightStrength, lightVec, normal, to_eye, mat);
}

#ifndef DEPTH_TEXTURE_SIZE
#    define DEPTH_TEXTURE_SIZE 1
#endif

float calc_lit_factor() {
    const mat4 to_range_01 =
        mat4(vec4(0.5, 0, 0, 0), vec4(0, 0.5, 0, 0), vec4(0, 0, 0.5, 0), vec4(0.5, 0.5, 0.5, 1.0));
    const float dx = 1.0 / float(DEPTH_TEXTURE_SIZE);
    const vec2 offsets[9] = { vec2(-dx, dx), vec2(0.0, dx),  vec2(dx, dx),    vec2(-dx, 0.0), vec2(0.0f, 0.0),
                              vec2(dx, 0.0), vec2(-dx, -dx), vec2(0.0f, -dx), vec2(dx, -dx) };

    // We have w = 1, so there is no effect. Can use `textureProj` I think
    const vec4 pos_wrt_clip = shadow_xform * vec4(fs_in.pos_w, 1.0);
    const vec4 pos_wrt_ndc = pos_wrt_clip / pos_wrt_clip.w;

    // Get xyz in range [0, 1]
    const vec3 pos_xyz_01 = (to_range_01 * pos_wrt_ndc).xyz;

    // Texture coordinate of this fragment into the depth texture
    const vec2 xy = pos_xyz_01.xy;

    // Depth of this fragment from light's point of view.
    const float depth = pos_xyz_01.z - 0.02f;

    float lit_factor = 0.0;

    for (int i = 0; i < 9; ++i) {
        const float is_closer = texture(comparing_sampler, vec3(xy + offsets[i], depth)).r;
        lit_factor += is_closer;
    }

    // Simple average
    lit_factor /= 9.0;

    return lit_factor;
}

float distance_sq(vec3 a, vec3 b) {
    vec3 d = a - b;
    return dot(d, d);
}

bool color_if_outside_scene(vec4 diffuse_albedo) {
    if (distance_sq(fs_in.pos_w, scene_bs.position) >= scene_bs.radius * scene_bs.radius) {
        fc = diffuse_albedo;
        return true;
    }
    return false;
}

void main() {
    Material mat = object_material;

#if 0
    if (color_if_outside_scene(mat.diffuse_albedo)) {
        return;
    }

#endif

    vec3 frag_color = vec3(0.0, 0.0, 0.0);
    vec3 normal_w = normalize(fs_in.normal_w);

    float lit_factor[NUM_DIR_LIGHTS];

#pragma unroll
    for (int i = 0; i < NUM_DIR_LIGHTS; ++i) {
        lit_factor[i] = 1.0;
    }

    // Only the first light casts shadow. Calculate the lit factor.

    lit_factor[0] = calc_lit_factor();

#pragma unroll
    for (int i = 0; i < NUM_DIR_LIGHTS; ++i) {
        DirLight l = dir_lights[i];
        frag_color +=
            calc_dir_light_contrib(l, mat, normal_w, normalize(eye_pos - fs_in.pos_w)) * lit_factor[i];
    }

    frag_color.xyz = filmic_tonemap(frag_color.xyz * 2.0);
    fc = vec4(frag_color, mat.diffuse_albedo.a);
}
