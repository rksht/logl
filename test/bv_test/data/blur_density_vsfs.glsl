#version 430 core

#include <common_defs.inc.glsl>

#if defined(DO_VS)

#define VS_MAIN main

out VsOutput { vec2 uv; }
o_vs;

void VS_MAIN() {
    const uint id = 2 - gl_VertexID;
    gl_Position.x = float(id / 2) * 4.0 - 1.0;
    gl_Position.y = float(id % 2) * 4.0 - 1.0;
    gl_Position.z = -1.0;
    gl_Position.w = 1.0;

    o_vs.uv.x = float(id / 2) * 2.0;
    o_vs.uv.y = float(id % 2) * 2.0;
}

#elif defined(DO_FS)

#define FS_MAIN main

in VsOutput { vec2 uv; }
i_fs;

const int g_kernel_size = GAUSSIAN_KERNEL_SIZE;
const float g_kernel_weights[] = GAUSSIAN_KERNEL_WEIGHTS;
const float g_normed_texel_width = NORMALIZED_TEXEL_WIDTH;
const float g_normed_texel_height = NORMALIZED_TEXEL_HEIGHT;

const vec2 offset_horizontal = vec2(g_normed_texel_width, 0.0);
const vec2 offset_vertical = vec2(0.0, g_normed_texel_height);

layout(binding = 0) uniform sampler2D attrib_sampler;

layout(location = 0) out float o_fs_attrib;

layout(binding = DENSITY_BLUR_UBO_BINDPOINT, std140) uniform DensityBlurUBO {
    uint b_horizontal_or_vertical;
};

void FS_MAIN() {

    const vec2 texel_size =
        b_horizontal_or_vertical == 0 ? vec2(g_normed_texel_width, 0.0) : vec2(0.0, g_normed_texel_height);

    float weighted_sum = 0.0;

    #if 0

#pragma unroll
    for (int i = -g_kernel_size / 2; i < g_kernel_size / 2; ++i) {
        const int array_index = i + g_kernel_size / 2;
        const vec2 sample_st =
            float(i) * texel_size + gl_FragCoord.xy * vec2(g_normed_texel_width, g_normed_texel_height);
        float attrib = texture(attrib_sampler, sample_st).x;
        weighted_sum += attrib * g_kernel_weights[array_index];
    }

    #else

    weighted_sum = texture(attrib_sampler, i_fs.uv).x;

    #endif

    o_fs_attrib = weighted_sum;
}

#else

#error "DO_VS or DO_VS"

#endif
