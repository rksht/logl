#version 430

in QuadVsOut {
    vec2 quad_st;
    vec3 eye_to_near_plane;
} fs_in;

layout(binding = POS_AND_NORMAL_TEXTURE_BINDING) uniform usampler2D pos_and_normal_sampler;
layout(binding = DEPTH_TEXTURE_BINDING) uniform sampler2D depth_sampler;

out vec4 frag_color;

struct Light {
    vec3 strength;
    float falloff_start;
    vec3 direction;
    float falloff_end;
    vec3 position;
    float spot_power;
};

struct Material {
    vec4 diffuse_albedo;
    vec3 fresnel_R0;
    float shininess;
};

// ------ Uniform blocks

layout(binding = SPHERE_MATERIAL_UBO_BINDING, std140) uniform BarMaterial { Material mat; };

layout(binding = LIGHT_PROPERTIES_UBO_BINDING, std140) uniform LightProperties {
    Light list[NUM_POINT_LIGHTS];
} light_properties;

layout(binding = VIEWPROJ_UBO_BINDING, std140) uniform ViewProjBlock {
    mat4 view;
    mat4 proj;
    vec4 eyepos_world;
} vp;

uniform uint calculate_in_eye_frame;

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

vec3 blinn_phong(vec3 light_strength, vec3 light_vec, vec3 normal, vec3 to_eye, Material mat) {
    const float m = mat.shininess * 256.0;
    vec3 half_vec = normalize(to_eye + light_vec);

    const float ndoth = max(0.0, dot(half_vec, normal));

    float roughness_factor = (m + 8.0) * pow(ndoth, m) / 8.0;
    vec3 fresnel_factor = schlick_fresnel(mat.fresnel_R0, half_vec, light_vec);
    vec3 spec_albedo = roughness_factor * fresnel_factor;

    // Our spec formula result goes outside [0.0 .. 1.0] range, but we are doing
    // LDR rendering, so forcefully scale it down a little.
    spec_albedo = spec_albedo / (spec_albedo + 1.0);

    return (mat.diffuse_albedo.rgb + spec_albedo) * light_strength;
}

vec3 compute_point_light(Light l, Material mat, vec3 pos, vec3 normal, vec3 to_eye) {
    vec3 light_vec = l.position - pos;
    float d = length(light_vec);

    if (d > l.falloff_end) {
        return vec3(0.0, 0.0, 0.0);
    }

    // Normalize light vector
    light_vec = light_vec / d;

    // Scale light down by Lambert's cosine law
    float ndotl = max(0.0, dot(light_vec, normal));
    vec3 light_strength = ndotl * l.strength;

    float att = calc_attenuation(d, l.falloff_start, l.falloff_end);
    light_strength = att * light_strength;

    return blinn_phong(light_strength, light_vec, normal, to_eye, mat);
    return vec3(d);
}

vec3 fragment_position_in_eyeframe(float window_depth) {
    const float ndc_z = 2.0 * window_depth - 1.0;
    const float E_1 = -1.0;
    const float T_1 = vp.proj[2][2];
    const float T_2 = vp.proj[3][2];
    const float R = T_2 / (ndc_z  - T_1 / E_1);

    const float V_z = R / E_1;

    return fs_in.eye_to_near_plane * V_z / (-Z_NEAR);
}

void main() {
    if (calculate_in_eye_frame == 1) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        uvec3 pn = texelFetch(pos_and_normal_sampler, ivec2(gl_FragCoord.xy), 0).xyz;
        float window_depth = texelFetch(depth_sampler, ivec2(gl_FragCoord.xy), 0).x;
        vec3 position_eyeframe = fragment_position_in_eyeframe(window_depth);
        vec2 pos_z_normal_x = unpackHalf2x16(pn.y);
        vec2 normal_yz = unpackHalf2x16(pn.z);

        const vec3 normal_world = vec3(pos_z_normal_x.y, normal_yz);
        const vec3 normal_view = (vp.view * vec4(normal_world, 0.0)).xyz;

        for (uint i = 0; i < NUM_POINT_LIGHTS; i++) {
            Light l = light_properties.list[i];
            l.position = (vp.view * vec4(l.position, 1.0)).xyz;
            frag_color.xyz +=
                compute_point_light(l, mat, position_eyeframe, normal_view, -position_eyeframe);
        }
    } else {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        uvec3 pn = texelFetch(pos_and_normal_sampler, ivec2(gl_FragCoord.xy), 0).xyz;
        vec2 pos_xy;
        vec2 pos_z_normal_x;
        vec2 normal_yz;

        pos_xy = unpackHalf2x16(pn.x);
        pos_z_normal_x = unpackHalf2x16(pn.y);
        normal_yz = unpackHalf2x16(pn.z);

        const vec3 position_world = vec3(pos_xy, pos_z_normal_x.x);
        const vec3 normal_world = vec3(pos_z_normal_x.y, normal_yz);

        for (uint i = 0; i < NUM_POINT_LIGHTS; i++) {
            Light l = light_properties.list[i];
            frag_color.xyz +=
                compute_point_light(l, mat, position_world, normal_world, vp.eyepos_world.xyz - position_world);
        }
    }
}
