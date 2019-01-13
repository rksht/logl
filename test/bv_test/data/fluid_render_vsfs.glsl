#version 430 core

struct Particle {
    vec2 position;
    vec2 velocity;
};

struct ParticleDensity {
    float density;
};

struct VsOutputStruct {
    vec2 position;
    float speed_sq;
    float density;
    float particle_index;
};

#include "simulation_constants.inc.glsl"

#if defined(RECORD_FLUID_ATTRIBUTES)



#endif

#if defined(FULL_SCREEN_FLUID_ATTRIBS)

#    if defined(DO_VS)

#        define VS_MAIN main

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

#    elif defined(DO_FS)

#        define FS_MAIN main

in VsOutput { vec2 uv; }
i_fs;

layout(binding = 0) uniform sampler2D attrib_sampler;

out vec4 frag_color;

void FS_MAIN() {
    // vec2 uv = vec2(gl_FragCoord.x / u_screen_wh.x, gl_FragCoord.y / u_screen_wh.y);

    vec4 attribs = texture(attrib_sampler, i_fs.uv).rgba;

    if (isnan(attribs.x) || isnan(attribs.y) || isnan(attribs.w) || isnan(attribs.z)) {
        frag_color = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    float r = attribs.x / (attribs.x + 1.0);
    float g = attribs.y / (attribs.y + 1.0);
    float b = attribs.z / (attribs.z + 1.0);

    // frag_color = vec4(r, g, b, 1.0);

    if (attribs.w != -1.0f) {
        frag_color = vec4(b, attribs.w / float(u_num_particles), 1.0 - b, 1.0);
        return;
    }
    frag_color = vec4(0.7, 0.7, 0.9, 1.0);
}

#    else

#        error "DO_VS or DO_FS"

#    endif

#endif
