#version 430 core

#include "definitions.inc.glsl"
#include "common_defs.inc.glsl"

in VsOut {
    vec3 pos_w;
    vec3 normal_w;
} fs_in;

vec4 get_diffuse_color() { return vec4(0.95, 0.20, 0.50, 1.0); }

DEFINE_CAMERA_UBLOCK(0, ublock_EyeBlock);

layout(binding = 1, std140) uniform ublock_PerObject
{
    mat4 world_from_local_xform;
    mat4 inv_world_from_local_xform;
    Material object_material; // Only using the material field
};

layout(binding = 2, std140) uniform DirLightsList { DirLight dir_lights[NUM_DIR_LIGHTS]; };

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

float clamp_interval(float f, float intervals[5], out float dist_from_max) {
    for (int i = 0; i < 5; ++i) {
        if (f < intervals[i]) {
            dist_from_max = intervals[i] - f;
            return intervals[i];
        }
    }
    return f;
}

vec3 blinn_phong_shade(vec3 light_strength, vec3 light_vec, vec3 normal, vec3 to_eye, Material mat) {
    const float m = mat.shininess * 256.0;
    vec3 half_vec = normalize(to_eye + light_vec);

    float ndoth_intervals[] = {0.0, 0.2, 0.4, 0.6, 1.0};

    float dist_from_max;

    const float ndoth = clamp_interval(max(0.0, dot(half_vec, normal)), ndoth_intervals, dist_from_max);
    // const float ndoth = max(0.0, dot(half_vec, normal));

    float roughness_factor = (m + 8.0) * pow(ndoth, m) / 8.0;
    vec3 fresnel_factor = schlick_fresnel(mat.fresnel_R0, half_vec, light_vec);
    vec3 spec_albedo = roughness_factor * fresnel_factor;

    // vec3 spec_albedo = vec3(0.2, 1.0, 0.4) * pow(ndoth, m);
    // Our spec formula result goes outside [0.0 .. 1.0] range, but we are doing LDR rendering, so forcefully
    // scale it down a little. A primitive tone-mapping function, one could say.
    spec_albedo = spec_albedo / (spec_albedo + 1.0);

    vec3 diffuse_color = mat.diffuse_albedo.rgb * light_strength;
    vec3 specular_color = spec_albedo * light_strength;
    // specular_color = vec3(clamp_interval(specular_color.x), clamp_interval(specular_color.y), clamp_interval(specular_color.z));
    // return diffuse_color + specular_color;
    return diffuse_color;
    // return vec3(roughness_factor);
}

float stepmix(float edge0, float edge1, float E, float x)
{
    float T = clamp(0.5 * (x - edge0 + E) / E, 0.0, 1.0);
    return mix(edge0, edge1, T);
}


#if 0
vec3 calc_dir_light_contrib(DirLight L, Material mat, vec3 normal, vec3 to_eye) {
    // The light vector aims opposite the dir the light rays travel.
    vec3 lightVec = -L.direction;

    float A = 0.1;
    float B = 0.4;
    float C = 0.6;
    float D = 0.8;
    float E = 1.0;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);

    float eps = 2.0 * fwidth(ndotl);
    if      (ndotl > A - eps && ndotl < A + eps) ndotl = stepmix(A, B, eps, ndotl);
    else if (ndotl > B - eps && ndotl < B + eps) ndotl = stepmix(B, C, eps, ndotl);
    else if (ndotl > C - eps && ndotl < C + eps) ndotl = stepmix(C, D, eps, ndotl);
    else if (ndotl < A) ndotl = 0.0;
    else if (ndotl < B) ndotl = B;
    else if (ndotl < C) ndotl = C;
    else ndotl = D;

    // L.diffuse = vec3(0.9, 0.9, 0.95);
    vec3 lightStrength = L.diffuse * ndotl;

    return blinn_phong_shade(lightStrength, lightVec, normal, to_eye, mat);
}

#endif

vec3 calc_dir_light_contrib(DirLight L, Material mat, vec3 normal, vec3 to_eye)
{
    // The light vector aims opposite the dir the light rays travel.
    vec3 lightVec = -L.direction;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    vec3 lightStrength = L.diffuse * ndotl;

    return blinn_phong_shade(lightStrength, lightVec, normal, to_eye, mat);
}

layout(location = 0) out vec4 fc;

void main() {
    // vec4 diffuse_color = texture(diffuse_sampler, fs_in.st).rgba;
    vec4 diffuse_color = get_diffuse_color();

    // fc = diffuse_color;
    vec3 frag_color = vec3(0.0, 0.0, 0.0);

    vec3 normal_w = normalize(fs_in.normal_w);

    Material mat = object_material;
    mat.diffuse_albedo = vec4(0.99, 0.90, 0.99, 1.0);

    for (int i = 0; i < NUM_DIR_LIGHTS; ++i) {
        DirLight l = dir_lights[i];
        frag_color += calc_dir_light_contrib(l, mat, normal_w, normalize(u_camPosition.xyz - fs_in.pos_w));
    }

    fc = vec4(frag_color, diffuse_color.a);
}

