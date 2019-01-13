#version 430 core

#include <common_defs.inc.glsl>

#include "simulation_constants.inc.glsl"

#if defined(DO_VS)

#define VS_MAIN main

// Full screen quad

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

layout(binding = 0) uniform sampler2D attrib_sampler;

out vec4 frag_color;

void FS_MAIN() {
    vec2 uv = vec2(gl_FragCoord.x / u_screen_wh.x, gl_FragCoord.y / u_screen_wh.y);

    float r = texture(attrib_sampler, i_fs.uv).r;

    if (isnan(r)) {
        frag_color = vec4(0.0, 0.0, 1.0, 1.0);
        return;
    }

    if (r == 0.0) {
        frag_color = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    r = abs(r);

    if (r > 1.0) {
        r = r / (r + 1.0);
    }

#if 0
    if (attribs.w != -1.0f) {
        frag_color = vec4(b, attribs.w / float(u_num_particles), 1.0 - b, 1.0);
        return;
    }
#endif

    // frag_color = vec4(0.7, 0.7, 0.9, 1.0);
    float b = float(int(r * 10000) % 100) / 100.0;
    frag_color = vec4(1.0 - b, float(int(r * 1000) % 100) / 100.0, b, 1.0);
    // frag_color = vec4(1.0, 1.0, 1.0, 1.0);
}

#else

#error "DO_VS or DO_FS"

#endif
