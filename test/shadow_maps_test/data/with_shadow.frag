#version 430 core

// Shader takes the albedo from a uniform block, not a texture.

#include "common_defs.inc.glsl"
#include "definitions.inc.glsl"

in VsOut
{
    vec3 pos_w;
    vec3 normal_w;
    vec3 tangent_w;
    vec2 st;
}
fs_in;

struct BoundingSphere {
    vec3 position;
    float radius;
};

layout(binding = 1) uniform sampler2DShadow comparing_sampler;

DEFINE_CAMERA_UBLOCK(0, ublock_EyeBlock);

layout(binding = 1, std140) uniform ublock_PerObject
{
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material; // Only using the material field
};

layout(binding = 2, std140) uniform DirLightsList { DirLight dir_lights[NUM_DIR_LIGHTS]; };

// The shadow transform takes a position in world space to clip space from the point of view of casting light.
layout(binding = 3, std140) uniform ShadowRelated
{
    mat4 shadow_xform;
    BoundingSphere scene_bs;
};

out vec4 fc;

float calc_attenuation(float d, float falloff_start, float falloff_end)
{
    float ratio = (falloff_end - d) / (falloff_end - falloff_start);
    return clamp(ratio, 0.0, 1.0);
}

vec3 schlick_fresnel(vec3 R0, vec3 normal, vec3 light_vec)
{
    float cos_incident_angle = clamp(dot(normal, light_vec), 0.0, 1.0);
    float f0 = 1.0 - cos_incident_angle;
    vec3 reflection_fraction = R0 + (1.0 - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflection_fraction;
}

vec3 blinn_phong_shade(vec3 light_strength, vec3 light_vec, vec3 normal, vec3 to_eye, Material mat)
{
    const float m = mat.shininess * 256.0;
    vec3 half_vec = normalize(to_eye + light_vec);

    const float ndoth = max(0.0, dot(half_vec, normal));

    float roughness_factor = (m + 8.0) * pow(ndoth, m) / 8.0;
    vec3 fresnel_factor = schlick_fresnel(mat.fresnel_R0, half_vec, light_vec);
    vec3 spec_albedo = roughness_factor * fresnel_factor;

    // Our spec formula result goes outside [0.0 .. 1.0] range, but we are doing LDR rendering, so forcefully
    // scale it down a little. A primitive tone-mapping function, one could say.
    spec_albedo = spec_albedo / (spec_albedo + 1.0);

    return (mat.diffuse_albedo.rgb + spec_albedo) * light_strength;
}

vec3 calc_dir_light_contrib(DirLight L, Material mat, vec3 normal, vec3 to_eye)
{
    // The light vector aims opposite the dir the light rays travel.
    vec3 lightVec = -L.direction;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    vec3 lightStrength = L.diffuse * ndotl;

    return blinn_phong_shade(lightStrength, lightVec, normal, to_eye, mat);
}

float calc_lit_factor()
{
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
    const float depth = pos_xyz_01.z;

    float lit_factor = 0.0;

    for (int i = 0; i < 9; ++i) {
        const float is_closer = texture(comparing_sampler, vec3(xy + offsets[i], depth)).r;
        lit_factor += is_closer;
    }

    // Simple average
    lit_factor /= 9.0;

    return lit_factor;
}

void main()
{
    Material mat = object_material;
    mat.diffuse_albedo = vec4(0.99, 0.90, 0.99, 1.0);

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
#if 1
        frag_color += calc_dir_light_contrib(l, mat, normal_w, normalize(u_camPosition.xyz - fs_in.pos_w)) *
          lit_factor[i];

#else
        frag_color += vec3(1) * lit_factor[i];
#endif
    }

    // frag_color.xyz = filmic_tonemap(frag_color.xyz * 2.0);
    fc = vec4(frag_color, 1.0);
    // fc = vec4(vec3(lit_factor[0]), 1.0);
}
