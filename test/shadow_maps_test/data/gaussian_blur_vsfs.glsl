#version 450 core

#if defined(DO_VERTEX_SHADER)

// ---- Full screen quad vertex shader

#define VS_main main

out VsOut {
    out vec2 uv;
} vsout;

void main() {
    const uint vid = gl_VertexID;
    gl_Position.x = float(vid / 2) * 4.0 - 1.0;
    gl_Position.y = vid == 0? 3.0 : -1.0;

    gl_Position.z = -1.0;
    gl_Position.w = 1.0;

    // vsout.uv.x = float(id / 2) * 2.0;
    // vsout.uv.y = float(id % 2) * 2.0;
    vsout.uv.x = gl_VertexID == 2? 2.0 : 0.0;
    vsout.uv.y = gl_VertexID == 0? 2.0 : 0.0;
}

#elif defined(DO_FRAGMENT_SHADER)
#define FS_main main

in VsOut {
    vec2 uv;
} i_fs;

const int g_kernel_size = GAUSSIAN_KERNEL_SIZE;
const float g_kernel_weights[] = GAUSSIAN_KERNEL_WEIGHTS;
const float g_normed_texel_width = NORMALIZED_TEXEL_WIDTH;
const float g_normed_texel_height = NORMALIZED_TEXEL_HEIGHT;
const vec2 offset_horizontal = vec2(g_normed_texel_width, 0.0);
const vec2 offset_vertical = vec2(0.0, g_normed_texel_height);

layout(binding = GAUSSIAN_BLUR_UBO_BINDPOINT, std140) uniform GaussianBlurUB
{
    uint b_horizontal_or_vertical;

    float fb_width;
    float fb_height;

    float weights[5];
    float offsets[5];
};

// layout(binding = FP16_COLOR_TEXTURE_BINDPOINT) uniform sampler2D rgba16f_sampler;
layout(binding = 0) uniform sampler2D rgba16f_sampler;

layout(location = 0) out vec4 o_fs;

vec4 get_normalized_middle(vec2 direction, vec2 texel_size, float offset, float i)
{
    // Get texture coordinate of the left texel
    vec2 u = gl_FragCoord.xy + i * direction;

    // Normalize it
    u /= vec2(fb_width, fb_height);

    // Add the offset (scale down by the normalized width first)
    u += offset * texel_size;
    // u += offset;

    // Sample

    return texture(rgba16f_sampler, u).xyzw;
}

vec2 make_offset_vec(float offset) {
    return b_horizontal_or_vertical == 0 ? vec2(offset, 0.0) : vec2(0.0, offset);
}

const float _offsets[5] = LERPED_GAUSSIAN_OFFSETS;
const float _weights[5] = LERPED_GAUSSIAN_WEIGHTS;

void main()
{
    // o_fs = vec4(1.0);

    // Offset in the horizontal on vertical direction
    const vec2 texel_size = b_horizontal_or_vertical == 0? vec2(1.0 / fb_width, 0.0) : vec2(0.0, 1.0 / fb_height);

#if defined(USE_5_LERPED_SAMPLES)
    // ---------------- 5 samples based gaussian blur. Hardcoded, can't tweak.

    // Sample the 5 positions.

#if 1

    // vec2 mid_pos = gl_FragCoord.xy / vec2(fb_width, fb_height);
    vec2 mid_pos = i_fs.uv;

    /*
    vec4 s[5];
    s[0] = texture(rgba16f_sampler, mid_pos + offsets[0] * texel_size);
    s[1] = texture(rgba16f_sampler, mid_pos + offsets[1] * texel_size);
    s[2] = texture(rgba16f_sampler, mid_pos + offsets[2] * texel_size);
    s[3] = texture(rgba16f_sampler, mid_pos + offsets[3] * texel_size);
    s[4] = texture(rgba16f_sampler, mid_pos + offsets[4] * texel_size);
    */

    // --- Multiply by weights and add

    vec4 weighted_sum = vec4(0.0);

    #pragma unroll
    for (int i = 0; i < 5; ++i) {
        // vec2 o = make_offset_vec(offsets[i]);
        weighted_sum += texture(rgba16f_sampler, mid_pos + (_offsets[i] * texel_size)) * _weights[i];
    }
    o_fs = vec4(weighted_sum.xyz, 1.0);

#else

    vec2 dim = vec2(fb_width, fb_height);

    o_fs = vec4(0.0);

    // o_fs = texture(rgba16f_sampler, vec2(gl_FragCoord) / dim) * _weights[0];
    for (int i = 0; i < 5; i++) {
        vec2 o = make_offset_vec(_offsets[i]);
        o_fs += texture(rgba16f_sampler, (vec2(gl_FragCoord) + make_offset_vec(_offsets[i])) / dim) * _weights[i];
        // o_fs += texture(rgba16f_sampler, (vec2(gl_FragCoord) - make_offset_vec(_offsets[i])) / dim) * _weights[i];
    }

#endif

#else
    // ---------------- Using 9 samples based vanilla gaussian blur

    vec4 weighted_sum = vec4(0.0);

    #pragma unroll
    for (int i = -g_kernel_size / 2; i < g_kernel_size / 2; ++i) {
        const int array_index = i + g_kernel_size / 2;
        const vec2 sample_st = float(i) * texel_size + gl_FragCoord.xy / vec2(fb_width, fb_height);
        vec4 color = texture(rgba16f_sampler, sample_st).xyzw;
        // vec4 color = texelFetch(rgba16f_sampler, gl_FragCoord - i * )
        weighted_sum += color * g_kernel_weights[array_index];
    }

    o_fs = vec4(weighted_sum.xyz, 1.0);

#endif
}



// --- Full screen gaussian blur fragment shader

#else

#error "Either DO_VERTEX_SHADER or DO_FRAGMENT_SHADER needs to be defined"

#endif
